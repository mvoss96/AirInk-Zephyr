#include "menu.hpp"

#include "app.hpp"

#include <stdint.h>
#include <zephyr/kernel.h>

#include "sensors/scd41.hpp"
#include "ui/display_ui.hpp"

namespace
{
	constexpr int64_t MENU_IDLE_MS = 30000;    // an untouched menu returns to the readings
	constexpr int64_t PROMPT_IDLE_MS = 120000; // long enough to carry the device outside
	constexpr int64_t CALIB_MS = 180000;       // the datasheet's warm-up before an FRC

	/* The bar advances in step with the sensor's own 5 s measurement interval: 36 steps
	 * over the three minutes. A panel refresh costs ~3.0 mAs and takes 0.85 s, so this
	 * adds ~108 mAs -- 4 % of the calibration -- while a per-second bar would spend
	 * ~540 mAs and keep the panel running back-to-back. Note the bar is not cheaper than
	 * the number it replaced: LV_Z_FULL_REFRESH means every flush writes the whole
	 * panel, whatever changed. It is honest, not cheap: a number showing seconds
	 * promises to tick every second, and could not keep that promise. */
	constexpr int64_t CALIB_DRAW_MS = 5000;

	/* No Sensor state. The sensor view belongs to main, which is the only place that has
	 * the readings -- giving this module a state for it is what used to force a callback
	 * back into main. */
	enum class State : uint8_t
	{
		Root,
		CalibPrompt,
		CalibRun,
		CalibFailed,
		Matter,
		ResetPrompt
	};

	State state = State::Root;
	ui::Menu cursor = ui::Menu::Calibrate;

	int64_t idle_at;     // Root / CalibPrompt time out here
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
		default: return "root";
		}
	}

	/** The only place `state` is assigned, so no transition can go unlogged.
	 *
	 * @param next where we are going; the same state is not a transition
	 */
	void go(State next)
	{
		if (next != state)
		{
			printk("[UI] menu %s -> %s\n", name(state), name(next));
		}
		state = next;
	}

	/** Advance the cursor to the next entry this build has.
	 *
	 * A build with no radio has no Matter row -- it is not drawn, so the cursor must not stop
	 * on it either, or a tap would appear to do nothing.
	 */
	ui::Menu next_entry(ui::Menu from)
	{
		ui::Menu e = from;
		do
		{
			e = (ui::Menu)(((int)e + 1) % (int)ui::Menu::Count);
		} while (!ui::menu_has(e) && e != from);
		return e;
	}

	/** Back to the list of entries, with the cursor where the user left it.
	 *
	 * A screen that only informs -- the Matter one -- returns here rather than dropping the user
	 * out to the readings: they came from the menu, and they may well want the next entry.
	 */
	void to_root()
	{
		go(State::Root);
		idle_at = k_uptime_get() + MENU_IDLE_MS;
		ui::set_menu(cursor);
	}

	/** The Matter screen: the QR while there is something to scan, the state once there is not.
	 *
	 * It only ever tells you something, so any gesture takes you back. Leaving the network is a
	 * menu entry of its own -- not a gesture hidden on a screen that reads like a status line.
	 */
	void to_matter()
	{
		go(State::Matter);
		idle_at = k_uptime_get() + PROMPT_IDLE_MS;
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

	/** Put the SCD41 into periodic measurement and start the three minutes.
	 *
	 * @return always Running: the failure has its own screen, which is still the menu
	 */
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

	/** Recalibrate against fresh air, then leave.
	 *
	 * @return Recalibrated on success; Running when the sensor answered 0xFFFF -- it did
	 *         not believe the air it measured was the target, changed nothing, and we stay
	 *         in the menu to say so
	 */
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

} // namespace

void menu::enter()
{
	state = State::Root;
	cursor = ui::Menu::Calibrate;
	idle_at = k_uptime_get() + MENU_IDLE_MS;
	printk("[UI] menu opened\n");
	ui::set_menu(cursor);
}

void menu::abort()
{
	if (state == State::CalibRun)
	{
		printk("[CAL] %s\n", (scd41::calibrate_abort() < 0)
								? "aborted, but the sensor did not confirm"
								: "aborted");
	}
	state = State::Root;
}

menu::Status menu::proceed(button::Event e)
{
	const int64_t now = k_uptime_get();

	switch (state)
	{
	case State::Root:
		if (e == button::Event::Short)
		{
			cursor = next_entry(cursor);
			idle_at = now + MENU_IDLE_MS;
			printk("[UI] menu cursor %d\n", (int)cursor);
			ui::set_menu(cursor);
		}
		else if (e == button::Event::Long)
		{
			switch (cursor)
			{
			case ui::Menu::Calibrate:
				to_calib_prompt();
				break;
			case ui::Menu::Matter:
				to_matter();
				break;
			case ui::Menu::FactoryReset:
				to_reset_prompt();
				break;
			default:
				return Status::Exited;
			}
		}
		else if (now >= idle_at)
		{
			printk("[UI] menu idle timeout\n");
			return Status::Exited;
		}
		return Status::Running;

	case State::CalibPrompt:
		// The one screen where a tap is the safe choice: a recalibration in the wrong
		// air cannot be undone.
		if (e == button::Event::Long)
		{
			return to_calib_run();
		}
		if (e == button::Event::Short || now >= idle_at)
		{
			return Status::Exited;
		}
		return Status::Running;

	case State::CalibRun:
		if (e == button::Event::Long)
		{
			abort();
			return Status::Exited;
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
		return (e != button::Event::None || now >= idle_at) ? Status::Exited : Status::Running;

	case State::Matter:
		// Nothing is being decided here, so any gesture takes the user back to the menu they came
		// from -- and so does the idle timeout, because an onboarding code left on an e-paper panel
		// is a code left on display. (The menu's own idle timeout then closes it soon after.)
		if (e != button::Event::None || now >= idle_at)
		{
			to_root();
		}
		return Status::Running;

	case State::ResetPrompt:
		if (e == button::Event::Long)
		{
			printk("[UI] factory reset confirmed\n");
			return Status::FactoryReset;
		}
		return (e != button::Event::None || now >= idle_at) ? Status::Exited : Status::Running;
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
