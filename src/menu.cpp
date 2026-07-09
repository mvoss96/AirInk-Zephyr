#include "menu.hpp"

#include <stdint.h>
#include <zephyr/kernel.h>

#include "sensors/scd41.hpp"
#include "ui/display_ui.hpp"

namespace
{
	constexpr int64_t MENU_IDLE_MS = 30000;    // an untouched menu returns to the readings
	constexpr int64_t PROMPT_IDLE_MS = 120000; // long enough to carry the device outside
	constexpr int64_t CALIB_MS = 180000;       // the datasheet's warm-up before an FRC

	/* The countdown is redrawn every 15 s, not every second. A partial refresh costs
	 * ~3 mAs, so a per-second countdown would spend ~540 mAs to animate a number the
	 * user glances at twice. */
	constexpr int64_t COUNTDOWN_DRAW_MS = 15000;

	enum class State : uint8_t
	{
		Sensor,
		Menu,
		CalibPrompt,
		CalibRun
	};

	State state = State::Sensor;
	ui::Menu cursor = ui::Menu::Calibrate;
	bool recalibrated;
	void (*show_sensor)();

	int64_t idle_at = INT64_MAX;     // Menu / CalibPrompt time out here
	int64_t calib_end_at;            // CalibRun sends the FRC here
	int64_t next_countdown_draw_at;  // CalibRun redraws here

	void to_sensor()
	{
		state = State::Sensor;
		idle_at = INT64_MAX;
		if (show_sensor)
		{
			show_sensor();
		}
	}

	void to_menu()
	{
		state = State::Menu;
		cursor = ui::Menu::Calibrate;
		idle_at = k_uptime_get() + MENU_IDLE_MS;
		ui::set_menu(cursor);
	}

	void to_calib_prompt()
	{
		state = State::CalibPrompt;
		idle_at = k_uptime_get() + PROMPT_IDLE_MS;
		ui::set_calib_prompt();
	}

	void to_calib_run()
	{
		if (scd41::calibrate_begin() < 0)
		{
			printk("[CAL] could not start periodic measurement\n");
			ui::set_error("CALIBRATION FAILED", "Sensor did not respond");
			state = State::Sensor;
			idle_at = INT64_MAX;
			return;
		}

		const int64_t now = k_uptime_get();
		state = State::CalibRun;
		idle_at = INT64_MAX; // no idle timeout: nothing is waiting on the user
		calib_end_at = now + CALIB_MS;
		next_countdown_draw_at = now + COUNTDOWN_DRAW_MS;
		printk("[CAL] warming up for %lld s\n", CALIB_MS / 1000);
		ui::set_calib_countdown((uint16_t)(CALIB_MS / 1000));
	}

	void abort_calib()
	{
		if (scd41::calibrate_abort() < 0)
		{
			printk("[CAL] aborted, but the sensor did not confirm\n");
		}
		else
		{
			printk("[CAL] aborted by the user\n");
		}
		to_sensor();
	}

	void finish_calib()
	{
		int16_t correction = 0;
		if (scd41::calibrate_finish(menu::CALIB_TARGET_PPM, &correction) < 0)
		{
			// The sensor answers 0xFFFF when the air it measured cannot plausibly be
			// the target. Nothing was changed, so there is nothing to undo.
			printk("[CAL] rejected: was the device really in fresh air?\n");
			ui::set_error("CALIBRATION FAILED", "Was it really outside?");
			state = State::Sensor;
			idle_at = INT64_MAX;
			return;
		}

		printk("[CAL] done, corrected by %+d ppm\n", correction);
		recalibrated = true;
		to_sensor();
	}

} // namespace

void menu::init(void (*show_sensor_view)())
{
	show_sensor = show_sensor_view;
}

bool menu::active()
{
	return state != State::Sensor;
}

bool menu::holds_sensor()
{
	return state == State::CalibRun;
}

bool menu::take_recalibrated()
{
	const bool was = recalibrated;
	recalibrated = false;
	return was;
}

void menu::force_exit()
{
	if (state == State::CalibRun)
	{
		abort_calib();
		return;
	}
	to_sensor();
}

void menu::on_button(button::Event e)
{
	if (e == button::Event::None)
	{
		return;
	}

	switch (state)
	{
	case State::Sensor:
		if (e == button::Event::Long)
		{
			to_menu();
		}
		break;

	case State::Menu:
		idle_at = k_uptime_get() + MENU_IDLE_MS;
		if (e == button::Event::Short)
		{
			cursor = (ui::Menu)(((int)cursor + 1) % (int)ui::Menu::Count);
			ui::set_menu(cursor);
		}
		else if (cursor == ui::Menu::Calibrate)
		{
			to_calib_prompt();
		}
		else
		{
			to_sensor();
		}
		break;

	case State::CalibPrompt:
		// The one screen where a tap is the safe choice: a recalibration in the wrong
		// air cannot be undone.
		(e == button::Event::Long) ? to_calib_run() : to_sensor();
		break;

	case State::CalibRun:
		if (e == button::Event::Long)
		{
			abort_calib();
		}
		break;
	}
}

int64_t menu::deadline_ms()
{
	if (state != State::CalibRun)
	{
		return idle_at;
	}
	return (next_countdown_draw_at < calib_end_at) ? next_countdown_draw_at : calib_end_at;
}

void menu::on_deadline()
{
	const int64_t now = k_uptime_get();

	switch (state)
	{
	case State::Menu:
	case State::CalibPrompt:
		if (now >= idle_at)
		{
			to_sensor();
		}
		break;

	case State::CalibRun:
		if (now >= calib_end_at)
		{
			finish_calib();
		}
		else if (now >= next_countdown_draw_at)
		{
			ui::set_calib_countdown((uint16_t)((calib_end_at - now + 999) / 1000));
			next_countdown_draw_at = now + COUNTDOWN_DRAW_MS;
		}
		break;

	case State::Sensor:
		break;
	}
}
