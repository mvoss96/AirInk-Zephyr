#include "menu.hpp"

#include "app.hpp"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <zephyr/kernel.h>

#include "prefs.hpp"
#include "sensors/scd41.hpp"
#include "ui/display_ui.hpp"
#include "ui/quantize.hpp"

/* ============================================================================================
 * The menu, as data.
 *
 * Everything below the tables is machinery. Everything a person would want to change -- a new
 * setting, a new row, a different order -- is IN the tables. That is the whole point of them: the
 * menu had grown five kinds of entry (a sub-menu, a screen, a toggle, a number, a way out) and was
 * spelling each one out by hand, in a switch that got a new case per feature and in a display that
 * knew every entry by name. A second list would have been a second copy of both.
 *
 * So: a row says what KIND it is. The machinery knows how each kind behaves -- a toggle flips where
 * it stands and rewrites its own label, a number opens the one editor there is, a screen has its own
 * little flow -- and it knows it once.
 * ============================================================================================ */

namespace
{
	constexpr int64_t MENU_IDLE_MS = 30000;	   // an untouched menu returns to the readings
	constexpr int64_t PROMPT_IDLE_MS = 120000; // long enough to carry the device outside
	constexpr int64_t CALIB_MS = 180000;	   // the datasheet's warm-up before an FRC

	/* The bar advances in step with the sensor's own 5 s measurement interval: 36 steps
	 * over the three minutes. A panel refresh costs ~3.0 mAs and takes 0.85 s, so this
	 * adds ~108 mAs -- 4 % of the calibration -- while a per-second bar would spend
	 * ~540 mAs and keep the panel running back-to-back. Note the bar is not cheaper than
	 * the number it replaced: LV_Z_FULL_REFRESH means every flush writes the whole
	 * panel, whatever changed. It is honest, not cheap: a number showing seconds
	 * promises to tick every second, and could not keep that promise. */
	constexpr int64_t CALIB_DRAW_MS = 5000;

	// ---- what a row can be ------------------------------------------------------------------

	/* The menus, by name. They are menu.cpp's and nobody else's: the panel has one menu view and draws
	 * whatever list of strings it is handed, so this enum exists to index the tables below and for a
	 * Submenu row to say where it goes. */
	enum class List : uint8_t
	{
		Root,
		Calibrate,
		Count,
	};

	enum class Kind : uint8_t
	{
		Submenu, // opens another list
		Screen,  // opens a view with a flow of its own
		Toggle,	 // flips where it stands; the row shows which way it is
		Number,	 // opens the one-button editor; the row shows the value
		Leave,	 // out of this list: back to its parent, or out of the menu altogether
	};

	enum class Screen : uint8_t
	{
		CalibCo2,
		Matter,
		FactoryReset,
	};

	enum class Toggle : uint8_t
	{
		Units,
		AutoCalib,
		Count,
	};

	enum class Number : uint8_t
	{
		TempOffset,
		Altitude,
		Count,
	};

	struct Row
	{
		Kind kind;
		const char *label; // the fixed part; a Toggle or a Number appends ": <value>"
		uint8_t id = 0;	   // a List, a Screen, a Toggle or a Number -- whichever `kind` says
		bool (*present)() = nullptr; // nullptr = always; else the row exists only where it is true
	};

	// ---- the toggles and the numbers, once each ---------------------------------------------

	struct ToggleDef
	{
		const char *off, *on; // what the row shows for the value
		bool (*get)();
		void (*set)(bool);
	};

	struct NumberDef
	{
		const char *title;	// the editor's headline
		const char *unit;	// what the number is measured in
		int lo, hi, step;	// in stored units, and it wraps: hi steps back round to lo
		uint8_t decimals;	// 1 -> the stored value is tenths
		const char *hint;	// what a tap does
		const char *(*sub)(int pending); // the line under the number; nullptr = none
		int (*get)();
		void (*set)(int);
	};

	// -- units: the one toggle that is not about the sensor ------------------------------------

	bool units_get() { return ui::temp_unit_shown() == ui::TempUnit::Fahrenheit; }
	void units_set(bool fahrenheit)
	{
		const ui::TempUnit u = fahrenheit ? ui::TempUnit::Fahrenheit : ui::TempUnit::Celsius;
		prefs::set_temp_unit(u);
		ui::set_temp_unit(u);
		app::publish_unit(u); // so a controller's copy agrees; no-op in a build with no network
	}

	// -- the sensor's trim: prefs remembers it, the sensor is told ------------------------------

	void push_trim() { scd41::set_trim(prefs::trim()); }

	bool asc_get() { return prefs::trim().auto_calib; }
	void asc_set(bool on)
	{
		prefs::set_auto_calib(on);
		push_trim();
	}

	int offset_get() { return prefs::trim().temp_offset_x10; }
	void offset_set(int v)
	{
		prefs::set_temp_offset_x10(v);
		push_trim();

		// The last reading was taken under the OLD offset, and no measurement can happen while the
		// menu is up. Keeping it would make the editor predict against a number that is already wrong
		// -- re-open it after a save and it would quote the reading from before the change. Better to
		// say "no reading yet" for the half minute until there is one again.
		app::forget_last_temp();
	}

	int altitude_get() { return prefs::trim().altitude_m; }
	void altitude_set(int v)
	{
		prefs::set_altitude_m(v);
		push_trim();
	}

	/** What the panel WILL say once a pending offset takes effect.
	 *
	 * This is the line that makes a one-button number bearable. The offset is a subtraction the
	 * sensor performs, so raising it by half a degree lowers the next reading by half a degree --
	 * which is arithmetic the user should not have to do while holding a thermometer. The panel does
	 * it: they tap until this line agrees with what they are holding, and hold to keep it.
	 *
	 * It says "will read" and not "reads", because it is a prediction: the sensor is trimmed at once,
	 * but the number on the sensor view is the last one measured, and the next measurement is up to
	 * half a minute away.
	 */
	const char *offset_sub(int pending_x10)
	{
		static char buf[40];

		const int32_t last = app::last_temp_x100();
		if (last == INT32_MIN)
		{
			return "No reading yet";
		}

		// The prediction, in Celsius: what was measured, less the change we are about to make.
		const int32_t delta_x100 = (pending_x10 - offset_get()) * 10;
		const int32_t c_x100 = last - delta_x100;

		// ...and shown in whatever unit the panel is in, because the thermometer in the user's hand
		// is in that unit too.
		if (ui::temp_unit_shown() == ui::TempUnit::Fahrenheit)
		{
			snprintf(buf, sizeof(buf), "Will read %d \xC2\xB0"
									   "F",
					 (int)(ui::quantize_temp_f_x100(c_x100) / 100));
		}
		else
		{
			const int32_t q = ui::quantize_temp_x100(c_x100);
			snprintf(buf, sizeof(buf), "Will read %d.%d \xC2\xB0"
									   "C",
					 (int)(q / 100), (int)(abs(q % 100) / 10));
		}
		return buf;
	}

	const char *altitude_sub(int) { return "Above sea level"; }

	/* In enum order, and that order is the contract: C++ has no designated initialisers for arrays --
	 * those are a C thing -- so the static_assert can only catch a missing entry, not a swapped one. */
	const ToggleDef TOGGLES[] = {
		{"\xC2\xB0"
								"C",
								"\xC2\xB0"
								"F",
								units_get, units_set},
		{"Off", "On", asc_get, asc_set},
	};
	static_assert(sizeof(TOGGLES) / sizeof(TOGGLES[0]) == (size_t)Toggle::Count, "a toggle with no definition");

	const NumberDef NUMBERS[] = {
		{"TEMP OFFSET",
									 "\xC2\xB0"
									 "C",
									 0, 200, 5, 1, "Tap = +0.5     Hold = save", offset_sub,
									 offset_get, offset_set},
		{"ALTITUDE", "m", 0, 3000, 100, 0, "Tap = +100     Hold = save",
								   altitude_sub, altitude_get, altitude_set},
	};
	static_assert(sizeof(NUMBERS) / sizeof(NUMBERS[0]) == (size_t)Number::Count, "a number with no definition");

	// ---- THE MENU ---------------------------------------------------------------------------

	const Row ROOT[] = {
		{Kind::Submenu, "Calibrate", (uint8_t)List::Calibrate},
		{Kind::Toggle, "Units", (uint8_t)Toggle::Units},
		{Kind::Screen, "Matter", (uint8_t)Screen::Matter, app::has_radio},
		{Kind::Screen, "Factory reset", (uint8_t)Screen::FactoryReset, app::can_factory_reset},
		{Kind::Leave, "Exit"},
	};

	const Row CALIBRATE[] = {
		{Kind::Screen, "Recalibrate CO2", (uint8_t)Screen::CalibCo2},
		{Kind::Number, "Temp offset", (uint8_t)Number::TempOffset},
		{Kind::Number, "Altitude", (uint8_t)Number::Altitude},
		{Kind::Toggle, "Auto-calib", (uint8_t)Toggle::AutoCalib},
		{Kind::Leave, "Back"},
	};

	/* Both lists are already AT the panel's limit, and draw() below fills fixed arrays of that size.
	 * A sixth row -- which the header of this file cheerfully invites -- would write past the end of
	 * them, and then show_list() would refuse the list and draw nothing, so the corruption would be
	 * the only symptom. The panel's limit is a hard limit; say so here, where the row would be added.
	 * (A list that genuinely needs six entries needs a sub-menu, which now costs one row.) */
	static_assert(sizeof(ROOT) / sizeof(ROOT[0]) <= ui::LIST_MAX_ROWS, "the root menu is too long");
	static_assert(sizeof(CALIBRATE) / sizeof(CALIBRATE[0]) <= ui::LIST_MAX_ROWS,
				  "the Calibrate menu is too long");

	struct ListDef
	{
		const Row *rows;
		int count;
		List parent; // where a Leave row goes; Root leaves the menu entirely
	};

	const ListDef LISTS[] = {
		/* Root      */ {ROOT, (int)(sizeof(ROOT) / sizeof(ROOT[0])), List::Root},
		/* Calibrate */ {CALIBRATE, (int)(sizeof(CALIBRATE) / sizeof(CALIBRATE[0])), List::Root},
	};
	static_assert(sizeof(LISTS) / sizeof(LISTS[0]) == (size_t)List::Count,
				  "a list with no definition");

	// ---- state ------------------------------------------------------------------------------

	/* Only the things that have a FLOW get a state. A toggle has none -- it happens and the menu is
	 * still there. A number has one, and one is all of them share. */
	enum class State : uint8_t
	{
		List, // a menu is on screen; `list` says which
		CalibPrompt,
		CalibRun,
		CalibFailed,
		Matter,
		ResetPrompt,
		Editing, // the number editor; `editing` says which number
	};

	State state = State::List;
	List list = List::Root;
	int cursor[(int)List::Count]; // where the user left each list

	Number editing = Number::TempOffset;
	int pending; // the value being turned, not yet saved

	int64_t idle_at;	  // every state that can be walked away from times out here
	int64_t calib_end_at; // CalibRun sends the FRC here
	int64_t next_draw_at; // CalibRun advances the bar here

	const char *name(State s)
	{
		switch (s)
		{
		case State::CalibPrompt: return "calib-prompt";
		case State::CalibRun: return "calib-run";
		case State::CalibFailed: return "calib-failed";
		case State::Matter: return "matter";
		case State::ResetPrompt: return "reset-prompt";
		case State::Editing: return "editing";
		default: return "list";
		}
	}

	/** The only place `state` is assigned, so no transition can go unlogged. */
	void go(State next)
	{
		if (next != state)
		{
			printk("[UI] menu %s -> %s\n", name(state), name(next));
		}
		state = next;
	}

	// ---- drawing a list ---------------------------------------------------------------------

	/** Whether a row exists on this device. A build with no radio has nothing to pair over, and
	 * nothing to reset -- so those rows are not drawn, and the cursor must not stop on them either,
	 * or a tap would appear to do nothing. */
	bool visible(const Row &r) { return r.present == nullptr || r.present(); }

	/** Everything on screen, in one place: the rows that exist, their labels with their values in
	 * them, and the cursor.
	 *
	 * The labels are rebuilt from the current values every time, which is what lets a toggle or a
	 * saved number show up in its row without anything being told to go and rewrite it. show_list()
	 * compares against what is already on the panel, so a list that has not changed costs nothing.
	 *
	 * @param sel which of the VISIBLE rows the cursor is on
	 */
	void draw(int sel)
	{
		const ListDef &def = LISTS[(int)list];

		static char text[ui::LIST_MAX_ROWS][32];
		const char *labels[ui::LIST_MAX_ROWS];
		int n = 0;

		for (int i = 0; i < def.count; i++)
		{
			const Row &r = def.rows[i];
			if (!visible(r))
			{
				continue;
			}
			switch (r.kind)
			{
			case Kind::Toggle:
			{
				const ToggleDef &t = TOGGLES[r.id];
				snprintf(text[n], sizeof(text[n]), "%s: %s", r.label, t.get() ? t.on : t.off);
				break;
			}
			case Kind::Number:
			{
				const NumberDef &d = NUMBERS[r.id];
				const int v = d.get();
				// The stored value is in tenths when decimals==1, so scale to the hundredths format_x100
				// speaks. (These are never negative, but the one formatter is the one that is right.)
				char num[12];
				ui::format_x100(num, sizeof(num), d.decimals == 1 ? v * 10 : v * 100, d.decimals);
				snprintf(text[n], sizeof(text[n]), "%s: %s %s", r.label, num, d.unit);
				break;
			}
			default:
				snprintf(text[n], sizeof(text[n]), "%s", r.label);
				break;
			}
			labels[n] = text[n];
			n++;
		}

		ui::show_list(labels, n, sel);
	}

	/** The row the cursor is on, skipping the ones this build does not have. */
	const Row &row_at(int sel)
	{
		const ListDef &def = LISTS[(int)list];
		int n = 0;
		for (int i = 0; i < def.count; i++)
		{
			if (visible(def.rows[i]) && n++ == sel)
			{
				return def.rows[i];
			}
		}
		return def.rows[def.count - 1]; // unreachable: sel is always a visible row
	}

	int visible_count()
	{
		const ListDef &def = LISTS[(int)list];
		int n = 0;
		for (int i = 0; i < def.count; i++)
		{
			n += visible(def.rows[i]) ? 1 : 0;
		}
		return n;
	}

	/** Show a list, with the cursor where the user left it. */
	void to_list(List l)
	{
		list = l;
		go(State::List);
		idle_at = k_uptime_get() + MENU_IDLE_MS;
		draw(cursor[(int)l]);
	}

	// ---- the screens, each with its own flow -------------------------------------------------

	/** The Matter screen: the QR while there is something to scan, the state once there is not.
	 *
	 * It only ever tells you something, so any gesture takes you back. Leaving the network is a
	 * menu entry of its own -- not a gesture hidden on a screen that reads like a status line.
	 *
	 * Showing the code also re-opens the commissioning window. The radio only listens for an hour
	 * after boot, so on a device that has been sitting on a shelf the code alone would be a dead
	 * letter -- putting it on the panel is the moment the user means to use it, so that is the
	 * moment to listen.
	 *
	 * Leaving the screen does NOT close the window again, and that is on purpose: the user leaves
	 * it precisely when they have just scanned the code, and the commissioner needs the next few
	 * seconds to answer. Closing on the way out would abort the join it was opened for. The window
	 * closes itself -- on success, or on its own timeout.
	 */
	void to_matter()
	{
		go(State::Matter);
		idle_at = k_uptime_get() + PROMPT_IDLE_MS;
		if (!app::commissioned())
		{
			app::open_pairing();
		}
		ui::show_matter(app::commissioned());
	}

	/** Ask before dropping every network the device is on.
	 *
	 * The same safety net as the calibration prompt, for the same reason: one button, and an
	 * answer that cannot be taken back. Tap is the reflex, so tap is the harmless one.
	 */
	void to_reset_prompt()
	{
		go(State::ResetPrompt);
		idle_at = k_uptime_get() + MENU_IDLE_MS;
		ui::set_reset_prompt();
	}

	void to_calib_prompt()
	{
		go(State::CalibPrompt);
		idle_at = k_uptime_get() + PROMPT_IDLE_MS;
		ui::set_calib_prompt();
	}

	/** Show what went wrong and wait for the user to take note.
	 *
	 * Nothing was changed, so there is nothing to decide: any gesture dismisses this, and
	 * so does the idle timeout, for a device left alone on a windowsill. The [CAL] log
	 * line above the call says whether the sensor refused the air it measured or never
	 * answered at all -- the screen only has room for the verdict.
	 */
	void to_calib_failed()
	{
		go(State::CalibFailed);
		idle_at = k_uptime_get() + MENU_IDLE_MS;
		ui::set_error("CALIBRATION FAILED", "PRESS TO CONTINUE");
	}

	/** Put the SCD41 into periodic measurement and start the three minutes. */
	menu::Status to_calib_run()
	{
		if (scd41::calibrate_begin() < 0)
		{
			printk("[CAL] could not start periodic measurement\n");
			to_calib_failed();
			return menu::Status::Running;
		}

		const int64_t now = k_uptime_get();
		go(State::CalibRun);
		calib_end_at = now + CALIB_MS;
		next_draw_at = now + CALIB_DRAW_MS;
		printk("[CAL] warming up for %lld s\n", CALIB_MS / 1000);
		ui::set_calib_progress(0);
		return menu::Status::Running;
	}

	/** Recalibrate against fresh air, then leave. */
	menu::Status finish_calib()
	{
		int16_t correction = 0;
		if (scd41::calibrate_finish(menu::CALIB_TARGET_PPM, &correction) < 0)
		{
			printk("[CAL] the sensor rejected the recalibration\n");
			to_calib_failed();
			return menu::Status::Running;
		}

		printk("[CAL] done, corrected by %+d ppm\n", correction);
		return menu::Status::Recalibrated;
	}

	// ---- the one number editor ---------------------------------------------------------------

	/** Draw the number being turned. */
	void draw_editing()
	{
		const NumberDef &d = NUMBERS[(int)editing];

		char num[16];
		ui::format_x100(num, sizeof(num), d.decimals == 1 ? pending * 10 : pending * 100, d.decimals);

		ui::set_value_edit(d.title, num, d.unit, d.sub ? d.sub(pending) : nullptr, d.hint);
	}

	void to_editing(Number n)
	{
		editing = n;
		pending = NUMBERS[(int)n].get();
		go(State::Editing);
		idle_at = k_uptime_get() + PROMPT_IDLE_MS;
		draw_editing();
	}

	// ---- what a hold on a row does ------------------------------------------------------------

	menu::Status activate(int sel)
	{
		const Row &r = row_at(sel);

		switch (r.kind)
		{
		case Kind::Submenu:
			to_list((List)r.id);
			return menu::Status::Running;

		case Kind::Toggle:
		{
			// No screen, no state: the row says which way it is, and a hold turns it the other way.
			// A screen would only be a place to press the button a second time.
			const ToggleDef &t = TOGGLES[r.id];
			t.set(!t.get());
			idle_at = k_uptime_get() + MENU_IDLE_MS;
			draw(sel); // the row rewrites itself, because draw() reads the value
			return menu::Status::Running;
		}

		case Kind::Number:
			to_editing((Number)r.id);
			return menu::Status::Running;

		case Kind::Screen:
			switch ((Screen)r.id)
			{
			case Screen::CalibCo2: to_calib_prompt(); break;
			case Screen::Matter: to_matter(); break;
			case Screen::FactoryReset: to_reset_prompt(); break;
			}
			return menu::Status::Running;

		case Kind::Leave:
			if (list == List::Root)
			{
				return menu::Status::Exited;
			}
			to_list(LISTS[(int)list].parent);
			return menu::Status::Running;
		}
		return menu::Status::Running;
	}

} // namespace

void menu::enter()
{
	for (int &c : cursor)
	{
		c = 0;
	}
	list = List::Root;
	printk("[UI] menu opened\n");
	to_list(List::Root);
}

void menu::abort()
{
	if (state == State::CalibRun)
	{
		printk("[CAL] %s\n", (scd41::calibrate_abort() < 0)
								 ? "aborted, but the sensor did not confirm"
								 : "aborted");
	}
	go(State::List);
}

menu::Status menu::proceed(button::Event e)
{
	const int64_t now = k_uptime_get();

	switch (state)
	{
	case State::List:
	{
		int &sel = cursor[(int)list];
		if (e == button::Event::Short)
		{
			sel = (sel + 1) % visible_count();
			idle_at = now + MENU_IDLE_MS;
			draw(sel);
		}
		else if (e == button::Event::Long)
		{
			return activate(sel);
		}
		else if (now >= idle_at)
		{
			printk("[UI] menu idle timeout\n");
			return Status::Exited;
		}
		return Status::Running;
	}

	case State::Editing:
	{
		const NumberDef &d = NUMBERS[(int)editing];
		if (e == button::Event::Short)
		{
			// It wraps, because one button has no way back. The range is small enough for that to be
			// a shrug rather than a punishment: 41 taps at the very worst, for a value set once.
			pending += d.step;
			if (pending > d.hi)
			{
				pending = d.lo;
			}
			idle_at = now + PROMPT_IDLE_MS;
			draw_editing();
		}
		else if (e == button::Event::Long)
		{
			d.set(pending); // writes it down and tells the sensor
			to_list(list);	// straight back to the row, which now shows the new value
		}
		else if (now >= idle_at)
		{
			// Walked away: keep nothing. A number left half-turned on a panel is not a decision.
			printk("[UI] editor idle timeout; %s unchanged\n", d.title);
			to_list(list);
		}
		return Status::Running;
	}

	case State::CalibPrompt:
		// The one screen where a tap is the safe choice: a recalibration in the wrong
		// air cannot be undone.
		if (e == button::Event::Long)
		{
			return to_calib_run();
		}
		if (e == button::Event::Short || now >= idle_at)
		{
			to_list(list);
		}
		return Status::Running;

	case State::CalibRun:
		if (e == button::Event::Long)
		{
			abort();		// takes the sensor back out of periodic mode
			to_list(list);	// ... and the user back to the menu they came from
			return Status::Running;
		}
		if (now >= calib_end_at)
		{
			// A full bar before the ~5 s CO2 read that follows, which happens with this
			// screen still up: the last step lands at ~97 % otherwise, and the wait then
			// looks like a stall rather than like a finished warm-up.
			ui::set_calib_progress(100);
			return finish_calib();
		}
		if (now >= next_draw_at)
		{
			const int64_t done = CALIB_MS - (calib_end_at - now);
			ui::set_calib_progress((uint8_t)(done * 100 / CALIB_MS));
			next_draw_at = now + CALIB_DRAW_MS;
		}
		return Status::Running;

	case State::CalibFailed:
		if (e != button::Event::None || now >= idle_at)
		{
			to_list(list);
		}
		return Status::Running;

	case State::Matter:
		// An onboarding code left on an e-paper panel is a code left on display, so the idle
		// timeout matters here as much as the gesture -- and both go back to the menu, whose own
		// timeout then closes it soon after.
		if (e != button::Event::None || now >= idle_at)
		{
			to_list(list);
		}
		return Status::Running;

	case State::ResetPrompt:
		if (e == button::Event::Long)
		{
			printk("[UI] factory reset confirmed\n");
			return Status::FactoryReset;
		}
		if (e != button::Event::None || now >= idle_at)
		{
			to_list(list);
		}
		return Status::Running;
	}
	return Status::Exited; // unreachable; a new state must be handled above
}

int64_t menu::deadline_ms()
{
	if (state != State::CalibRun)
	{
		return idle_at;
	}
	return (next_draw_at < calib_end_at) ? next_draw_at : calib_end_at;
}
