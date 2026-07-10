#include <stdlib.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/pm/device.h>

#include "input/button.hpp"
#include "menu.hpp"
#include "sensors/battery.hpp"
#include "sensors/scd41.hpp"
#include "ui/display_ui.hpp"
#include "version.hpp"

static const struct device *const console_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));

/** Resume or suspend the console UART.
 *
 * @param on true before printing, false afterwards -- a suspended UARTE lets the
 *           HFCLK stop, which is the difference between ~1 mA and ~60 uA idle
 */
static void console_uart(bool on)
{
	pm_device_action_run(console_dev, on ? PM_DEVICE_ACTION_RESUME : PM_DEVICE_ACTION_SUSPEND);
}

static constexpr int TICK_MS = 30000;			// 30 s between each measurement cycle (5 min CO2 + 30 s T+RH)
static constexpr int CO2_EVERY_TICKS = 10;		// full CO2 read every 10 ticks (5 min)
static constexpr int BATT_UNKNOWN = -1;			// battery percent when the ADC read failed
static constexpr int LOW_BATTERY_ENTER_PCT = 5; // battery percent at which the low-battery latch engages
static constexpr int LOW_BATTERY_EXIT_PCT = 8;	// battery percent at which the low-battery latch disengages

/* What the panel is showing. The two modes know nothing about each other: menu.cpp has
 * no sensor state, and nothing outside this file reads the measurements. */
enum class Mode : uint8_t
{
	Sensor,
	Menu
};

static Mode mode = Mode::Sensor;
static Scd41Reading last_reading; // held on screen between reads, and across the menu
static uint32_t tick_count;		  // number of measurement cycles since boot
static bool low_battery;		  // latched; while set the SCD41 is not read at all
static bool charging;

/** Stage the sensor view with whatever we last measured. */
static void show_sensor_view()
{
	ui::set_sensor(last_reading.co2_ppm, last_reading.temp_x100, last_reading.hum_x100);
}

/** Read the battery and update the low-battery latch.
 *
 * Hysteresis rather than a plain threshold: the smoothed percent drifts across a single
 * boundary, and each flap would be a view change, i.e. a full-refresh black flash.
 * Charging always releases the latch.
 *
 * @return the percent, or BATT_UNKNOWN if the ADC read failed (the latch then stands)
 */
static int measure_battery()
{
	BatteryReading b{};
	if (battery::sample(&b) != 0)
	{
		return BATT_UNKNOWN;
	}
	charging = b.charging;

	if (b.charging || b.bat_pct >= LOW_BATTERY_EXIT_PCT)
	{
		low_battery = false;
	}
	else if (b.bat_pct <= LOW_BATTERY_ENTER_PCT)
	{
		low_battery = true;
	}

	ui::set_battery(b.bat_pct, b.charging); // status bar only; never changes the view
	return b.bat_pct;
}

/** Read the SCD41 and stage the sensor view.
 *
 * A full CO2 single-shot on every CO2_EVERY_TICKS-th tick, a ~1000x cheaper T+RH read
 * otherwise; the last CO2 value stays on screen in between.
 *
 * @param batt_pct only for the log line
 */
static void measure_sensor(int batt_pct)
{
	const bool full_co2 = (tick_count % CO2_EVERY_TICKS) == 0;

	Scd41Reading r{};
	if ((full_co2 ? scd41::sample(&r) : scd41::sample_rht(&r)) != 0)
	{
		printk("SCD41: %s read failed\n", full_co2 ? "CO2" : "RHT");
		ui::set_error("SENSOR ERROR", "SCD41 read failed");
		return;
	}

	if (!full_co2)
	{
		r.co2_ppm = last_reading.co2_ppm; // keep the last CO2 shown
	}
	last_reading = r;

	printk("%s CO2 %u ppm  T %d.%02d C  RH %d.%02d %%  batt %d%%%s\n",
		   full_co2 ? "[CO2]" : "[RHT]", r.co2_ppm,
		   r.temp_x100 / 100, abs(r.temp_x100 % 100),
		   r.hum_x100 / 100, r.hum_x100 % 100,
		   batt_pct, charging ? " CHG" : "");

	show_sensor_view();
	tick_count++;
}

/** One measurement cycle.
 *
 * The battery is read in either mode -- the status bar is visible on every view. The
 * SCD41 is not touched while the user is in the menu: it may be mid-calibration, where a
 * single-shot command would be refused, and even if it is not, nobody is looking.
 *
 * A CO2 single-shot is ~70 mAs (~86 % of the budget), so a low battery suspends it
 * entirely and lets the last few percent coast at the ~60 uA idle while the panel holds
 * the warning without power. The warning outranks whatever the user was doing.
 */
static void do_tick()
{
	const int batt_pct = measure_battery();

	if (low_battery)
	{
		if (mode == Mode::Menu)
		{
			menu::abort(); // ends a calibration if one was running
			mode = Mode::Sensor;
		}
		printk("[LOW] batt %d%%%s  measurement suspended\n", batt_pct, charging ? " CHG" : "");
		ui::set_low_battery();
		return;
	}

	if (mode == Mode::Sensor)
	{
		measure_sensor(batt_pct);
	}
}

/** Bring up the display, sensors and button, then loop over measurement cycles.
 *
 * @return 0 when a fatal sensor error leaves its message on the panel; otherwise
 *         never returns
 */
int main(void)
{
	const bool display_ok = (ui::init() == 0);

	printk("AirInk v%s (%s %s) started (display %s)\n",
		   AIRINK_VERSION, __DATE__, __TIME__, display_ok ? "ok" : "FAILED");

	if (scd41::init() < 0)
	{
		printk("SCD41 not ready\n");
		ui::set_error("SENSOR ERROR", "SCD41 not found");
		ui::refresh();
		return 0; // leave the error on screen
	}
	if (battery::init() < 0)
	{
		printk("Battery ADC not ready (continuing without it)\n");
	}
	if (button::init() < 0)
	{
		printk("Button not ready (continuing without it)\n");
	}
	ui::log_pool("boot splash"); // every view is built; this is the resident cost

	/* The loop runs on main's own stack, not the system work queue: a CO2 single-shot
	 * blocks for ~5 s, and gpio-keys debounces on that queue -- blocking it would eat
	 * the very button presses this loop waits for.
	 *
	 * It wakes on whichever comes first: the measurement tick, a button, or something
	 * the menu is waiting on. The tick deadline is absolute, so a slow cycle or a long
	 * calibration does not make the cadence drift.
	 *
	 * The console is woken only while we are awake anyway; between iterations the UARTE
	 * is suspended so the HFCLK can stop (~1 mA versus ~60 uA idle).
	 */
	int64_t next_tick = k_uptime_get();
	bool heap_logged = false;
	while (true)
	{
		const int64_t menu_at = (mode == Mode::Menu) ? menu::deadline_ms() : INT64_MAX;
		uint16_t held_ms = 0;
		const button::Event e =
			button::wait_until((menu_at < next_tick) ? menu_at : next_tick, &held_ms);

		console_uart(true);

		if (e != button::Event::None)
		{
			// The held time, not just the verdict: the only way to see how close a
			// gesture came to LONG_PRESS_MS without guessing at the threshold.
			printk("[BTN] %s (%u ms)%s\n", (e == button::Event::Long) ? "hold" : "tap",
				   held_ms, low_battery ? " ignored, battery low" : "");
		}

		if (mode == Mode::Menu)
		{
			// Every way out of the menu ends here, and every one of them decides what
			// goes on the panel -- the menu itself only ever stages its own views.
			switch (menu::proceed(e))
			{
			case menu::Status::Running:
				break;
			case menu::Status::Recalibrated:
				// The retained CO2 value predates the new calibration; replace it now
				// rather than wait out the rest of the tick.
				tick_count = 0;
				next_tick = k_uptime_get();
				mode = Mode::Sensor;
				show_sensor_view();
				break;
			case menu::Status::Exited:
				mode = Mode::Sensor;
				show_sensor_view();
				break;
			case menu::Status::Rejected:
				mode = Mode::Sensor;
				ui::set_error("CALIBRATION FAILED", "Was it really outside?");
				break;
			case menu::Status::SensorError:
				mode = Mode::Sensor;
				ui::set_error("SENSOR ERROR", "SCD41 did not respond");
				break;
			}
		}
		else if (e == button::Event::Long && !low_battery)
		{
			// A calibration costs ~2600 mAs; do not start one on a dying cell.
			mode = Mode::Menu;
			menu::enter();
		}

		const int64_t now = k_uptime_get();
		if (now >= next_tick)
		{
			do_tick();
			while (next_tick <= now)
			{
				next_tick += TICK_MS;
			}
		}

		ui::refresh(); // exactly one panel refresh per iteration, whatever happened

		if (!heap_logged)
		{
			// After the first sensor render: the DSEG7-48 glyphs are the widest draw
			// buffers the UI ever asks for, so this is where the peak shows up.
			ui::log_pool("first render");
			heap_logged = true;
		}
		console_uart(false);
	}
}
