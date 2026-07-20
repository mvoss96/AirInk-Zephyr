#include "menu.hpp"

#include "net.hpp"

#include <stdint.h>
#include <stdio.h>
#include <zephyr/kernel.h>

#include "prefs.hpp"
#include "sensors/battery.hpp"
#include "sensors/scd41.hpp"
#include "ui/display_ui.hpp"
#include "ui/quantize.hpp"

/* The menu, as data: everything a person would change -- a new setting, a new row, a different
 * order -- is IN the tables below; everything under them is machinery that knows each row KIND
 * (sub-menu, screen, toggle, number, way out) exactly once. */

namespace
{
	constexpr int64_t MENU_IDLE_MS = 30000;	   // an untouched menu returns to the readings
	constexpr int64_t PROMPT_IDLE_MS = 120000; // long enough to carry the device outside
	constexpr int64_t CALIB_MS = 180000;	   // the datasheet's warm-up before an FRC

	// Fresh outdoor air, the only concentration a user can reliably stand in.
	constexpr uint16_t CALIB_TARGET_PPM = 420;

	/* The bar steps with the sensor's own 5 s interval: 36 refreshes x ~3 mAs = ~108 mAs, 4 % of
	 * the calibration. A per-second bar would cost 5x that and keep the panel running non-stop. */
	constexpr int64_t CALIB_DRAW_MS = 5000;

	// ---- what a row can be ------------------------------------------------------------------

	/* The menus, by name. They are menu.cpp's and nobody else's: the panel has one menu view and draws
	 * whatever list of strings it is handed, so this enum exists to index the tables below and for a
	 * Submenu row to say where it goes. */
	enum class List : uint8_t
	{
		Root,
		Calibrate,
		System,
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
		FwUpdate,
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
	//
	// A toggle or a number is a VIEW onto one stored setting: which one, and how to show it. prefs
	// does everything else (clamp, save, apply) -- so there are no setters here to forget an
	// aftermath in.

	struct ToggleDef
	{
		prefs::Id id;
		const char *off, *on; // what the row shows for the value
	};

	struct NumberDef
	{
		prefs::Id id;
		const char *title;	// the editor's headline
		const char *unit;	// what the number is measured in
		int step;			// in stored units; it wraps, and the range it wraps in is prefs's --
							// a value the editor offers but the store would refuse is a lie
		uint8_t decimals;	// 1 -> the stored value is tenths
		const char *hint;	// what a tap does
		const char *sub;	// the line under the number; nullptr = none
	};

	/* In enum order, and that order is the contract: C++ has no designated initialisers for arrays --
	 * those are a C thing -- so the static_assert can only catch a missing entry, not a swapped one. */
	const ToggleDef TOGGLES[] = {
		{prefs::Unit, "\xC2\xB0"
					  "C",
					  "\xC2\xB0"
					  "F"},
		{prefs::AutoCalib, "Off", "On"},
	};
	static_assert(sizeof(TOGGLES) / sizeof(TOGGLES[0]) == (size_t)Toggle::Count,
				  "a toggle with no definition");

	const NumberDef NUMBERS[] = {
		{prefs::TempOffset, "TEMP OFFSET",
		 "\xC2\xB0"
		 "C",
		 5, 1, "Tap = +0.5     Hold = save", "Subtracted from the reading"},
		{prefs::Altitude, "ALTITUDE", "m", 100, 0, "Tap = +100     Hold = save", "Above sea level"},
	};
	static_assert(sizeof(NUMBERS) / sizeof(NUMBERS[0]) == (size_t)Number::Count,
				  "a number with no definition");

	// ---- THE MENU ---------------------------------------------------------------------------

	/* Both System rows can be absent, and a sub-menu holding nothing but "Back" is a row that
	 * punishes whoever taps it -- so the row into it answers the same question its contents do. */
	bool has_system_rows() { return battery::charging() || net::can_factory_reset(); }

	const Row ROOT[] = {
		{Kind::Submenu, "Calibrate", (uint8_t)List::Calibrate},
		{Kind::Toggle, "Units", (uint8_t)Toggle::Units},
		{Kind::Screen, "Matter", (uint8_t)Screen::Matter, net::has_radio},
		{Kind::Submenu, "System", (uint8_t)List::System, has_system_rows},
		{Kind::Leave, "Exit"},
	};

	const Row CALIBRATE[] = {
		{Kind::Screen, "Recalibrate CO2", (uint8_t)Screen::CalibCo2},
		{Kind::Number, "Temp offset", (uint8_t)Number::TempOffset},
		{Kind::Number, "Altitude", (uint8_t)Number::Altitude},
		{Kind::Toggle, "Auto-calib", (uint8_t)Toggle::AutoCalib},
		{Kind::Leave, "Back"},
	};

	/* The update row exists only while USB is plugged: the UF2 drive it reboots into needs the
	 * cable anyway, and without one the device would sit in the bootloader until a manual reset. */
	const Row SYSTEM[] = {
		{Kind::Screen, "Firmware update", (uint8_t)Screen::FwUpdate, battery::charging},
		{Kind::Screen, "Factory reset", (uint8_t)Screen::FactoryReset, net::can_factory_reset},
		{Kind::Leave, "Back"},
	};

	/* Both lists are AT the panel's limit, and draw() fills fixed arrays of that size -- a sixth
	 * row would write past them. A list that needs six entries needs a sub-menu (costs one row). */
	static_assert(sizeof(ROOT) / sizeof(ROOT[0]) <= ui::LIST_MAX_ROWS, "the root menu is too long");
	static_assert(sizeof(CALIBRATE) / sizeof(CALIBRATE[0]) <= ui::LIST_MAX_ROWS,
				  "the Calibrate menu is too long");
	static_assert(sizeof(SYSTEM) / sizeof(SYSTEM[0]) <= ui::LIST_MAX_ROWS,
				  "the System menu is too long");

	struct ListDef
	{
		const Row *rows;
		int count;
		List parent; // where a Leave row goes; Root leaves the menu entirely
	};

	const ListDef LISTS[] = {
		/* Root      */ {ROOT, (int)(sizeof(ROOT) / sizeof(ROOT[0])), List::Root},
		/* Calibrate */ {CALIBRATE, (int)(sizeof(CALIBRATE) / sizeof(CALIBRATE[0])), List::Root},
		/* System    */ {SYSTEM, (int)(sizeof(SYSTEM) / sizeof(SYSTEM[0])), List::Root},
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
		UpdatePrompt,
		Editing, // the number editor; `editing` says which number
	};

	State state = State::List;
	List list = List::Root;
	int cursor[(int)List::Count]; // where the user left each list

	Number editing = Number::TempOffset;
	int pending;	// the value being turned, not yet saved
	bool matter_qr; // which half of the Matter screen is up: the QR, or the CONNECTED status

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
		case State::UpdatePrompt: return "update-prompt";
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

	/** Draw the current list: visible rows, labels with their values baked in, cursor on `sel`
	 * (counted over VISIBLE rows). Labels are rebuilt from the values every time -- that is how a
	 * toggled row rewrites itself -- and show_list() dedups, so an unchanged list costs nothing. */
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
				snprintf(text[n], sizeof(text[n]), "%s: %s", r.label,
						 prefs::get(t.id) ? t.on : t.off);
				break;
			}
			case Kind::Number:
			{
				const NumberDef &d = NUMBERS[r.id];
				const int v = prefs::get(d.id);
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

	/** The Matter screen, two halves by net::commissioned().
	 *
	 * The QR half: opening it also opens the commissioning window (the moment the code is on the
	 * panel is the moment somebody means to scan it), and it has NO idle timeout -- an
	 * uncommissioned device's one job is this code, and e-paper keeps it up for free. idle_at is
	 * only the poll that lets proceed() notice a commissioner without a gesture.
	 *
	 * The CONNECTED half: a status page, read and left, on the usual prompt timeout. */
	void to_matter()
	{
		go(State::Matter);
		matter_qr = !net::commissioned();
		if (matter_qr)
		{
			net::open_pairing();
			idle_at = k_uptime_get() + MENU_IDLE_MS; // the poll, not a timeout
		}
		else
		{
			idle_at = k_uptime_get() + PROMPT_IDLE_MS;
		}
		ui::show_matter(!matter_qr);
	}

	/** Ask before dropping every network. One button, an answer that cannot be taken back: tap is
	 * the reflex, so tap is the harmless one. */
	void to_reset_prompt()
	{
		go(State::ResetPrompt);
		idle_at = k_uptime_get() + MENU_IDLE_MS;
		ui::set_reset_prompt();
	}

	/** Ask before rebooting into the UF2 bootloader. Same one-button grammar as the reset prompt:
	 * tap is the reflex, so tap backs out. */
	void to_update_prompt()
	{
		go(State::UpdatePrompt);
		idle_at = k_uptime_get() + MENU_IDLE_MS;
		ui::set_error("FIRMWARE UPDATE", "HOLD TO START");
	}

	void to_calib_prompt()
	{
		go(State::CalibPrompt);
		idle_at = k_uptime_get() + PROMPT_IDLE_MS;
		ui::set_calib_prompt();
	}

	/** Show the verdict and wait for a gesture (or the idle timeout, for a device left on a
	 * windowsill). Nothing was changed, so there is nothing to decide; the [CAL] log line above
	 * each call carries the detail the screen has no room for. */
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
		if (scd41::calibrate_finish(CALIB_TARGET_PPM, &correction) < 0)
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

		ui::set_value_edit(d.title, num, d.unit, d.sub, d.hint);
	}

	void to_editing(Number n)
	{
		editing = n;
		pending = prefs::get(NUMBERS[(int)n].id);
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
			prefs::set(t.id, prefs::get(t.id) ? 0 : 1);
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
			case Screen::FwUpdate: to_update_prompt(); break;
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

void menu::enter_matter()
{
	for (int &c : cursor)
	{
		c = 0;
	}
	list = List::Root;
	to_matter();
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
			if (pending > prefs::hi(d.id))
			{
				pending = prefs::lo(d.id);
			}
			idle_at = now + PROMPT_IDLE_MS;
			draw_editing();
		}
		else if (e == button::Event::Long)
		{
			prefs::set(d.id, pending); // writes it down, and tells whoever else holds a copy
			to_list(list);			   // straight back to the row, which now shows the new value
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
		if (matter_qr)
		{
			/* The QR half. Scanned? The payoff is the readings, not a status page. Declined? Cut
			 * the advertising window short and get on with the readings too. Either way the menu
			 * is done -- this is the one screen that exits to the sensor view directly. */
			if (net::commissioned())
			{
				printk("[MTR] commissioned; on to the readings\n");
				return Status::Exited;
			}
			if (e != button::Event::None)
			{
				printk("[MTR] onboarding code dismissed\n");
				net::dismiss_pairing();
				return Status::Exited;
			}
			if (now >= idle_at)
			{
				idle_at = now + MENU_IDLE_MS; // re-arm the commissioner poll; never a timeout
			}
			return Status::Running;
		}
		// The CONNECTED half: any gesture or the timeout goes back to the list.
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

	case State::UpdatePrompt:
		if (e == button::Event::Long)
		{
			printk("[UI] firmware update confirmed\n");
			return Status::FirmwareUpdate;
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
