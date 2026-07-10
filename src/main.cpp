#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/pm/device.h>

#include "app/menu.hpp"
#include "app/sensorview.hpp"
#include "input/button.hpp"
#include "sensors/battery.hpp"
#include "sensors/scd41.hpp"
#include "ui/display_ui.hpp"
#include "version.hpp"

static const struct device *const console_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));

// How often a measurement cycle runs. How much of it is a CO2 read is sensorview's call.
static constexpr int TICK_MS = 30000;

/** Sleep until the next measurement, or until the menu or the button needs us.
 *
 * Also keeps the measurement on a fixed 30 s grid: a ~5 s CO2 read must not push the
 * cadence, and three minutes of calibration must not leave six ticks to fire
 * back-to-back. Advancing here is also what stops a past deadline from turning the sleep
 * into a busy loop.
 *
 * @param[in,out] next_tick   uptime of the next measurement; moved onto the next grid
 *                            point ahead of now
 * @param         menu_active true while the menu has deadlines of its own
 * @return the gesture that ended the sleep, or Event::None if a deadline did
 */
static button::Event sleep_until_next_cycle(int64_t &next_tick, bool menu_active)
{
	const int64_t now = k_uptime_get();
	while (next_tick <= now)
	{
		next_tick += TICK_MS;
	}

	const int64_t menu_at = menu_active ? menu::deadline_ms() : INT64_MAX;

	// A live console UARTE keeps the HFCLK running: ~1 mA versus the ~60 uA idle floor.
	pm_device_action_run(console_dev, PM_DEVICE_ACTION_SUSPEND);
	uint16_t held_ms = 0;
	const button::Event e = button::wait_until((menu_at < next_tick) ? menu_at : next_tick, &held_ms);
	pm_device_action_run(console_dev, PM_DEVICE_ACTION_RESUME);

	if (e != button::Event::None)
	{
		// The held time, not just the verdict: the only way to see how close a gesture
		// came to LONG_PRESS_MS without guessing at the threshold.
		printk("[BTN] %s (%u ms)\n", (e == button::Event::Long) ? "hold" : "tap", held_ms);
	}
	return e;
}

/** Bring up the display, sensors and button, then loop over measurement cycles.
 *
 * The loop runs on main's own stack, not the system work queue: a CO2 single-shot blocks
 * for ~5 s, and gpio-keys debounces on that queue -- blocking it would eat the very
 * button presses this loop waits for.
 *
 * @return 0 when a fatal sensor error leaves its message on the panel; otherwise never
 *         returns
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

	int64_t next_tick = k_uptime_get();
	bool menu_active = false;
	button::Event e = button::Event::None;

	while (true)
	{
		const battery::State bs = battery::read();
		ui::set_battery(bs.pct, bs.charging); // status bar only; never a view change

		// A low battery outranks everything. A CO2 single-shot is ~70 mAs (~86 % of the
		// budget) and a calibration ~2600 mAs, so the last few percent coast at the
		// ~60 uA idle while the panel holds the warning without power.
		if (bs.low)
		{
			if (menu_active)
			{
				menu::abort(); // a calibration would leave the SCD41 in periodic mode
				menu_active = false;
			}
			printk("[LOW] batt %u%%%s  measurement suspended\n", bs.pct, bs.charging ? " CHG" : "");
			ui::set_low_battery();
		}
		else if (menu_active)
		{
			// The menu stages only its own views. Everything it cannot draw -- the
			// readings, an error -- is rendered here, because only main has them.
			const menu::Status s = menu::proceed(e);
			menu_active = (s == menu::Status::Running);

			switch (s)
			{
			case menu::Status::Recalibrated:
				sensorview::measure(true); // the retained CO2 predates the new calibration
				break;
			case menu::Status::Exited:
				sensorview::show();
				break;
			case menu::Status::Failed:
				ui::set_error("CALIBRATION FAILED", nullptr);
				break;
			case menu::Status::Running:
				break;
			}
		}
		else if (e == button::Event::Long)
		{
			menu_active = true;
			menu::enter();
		}
		else if (k_uptime_get() >= next_tick)
		{
			sensorview::measure();
		}

		ui::refresh(); // exactly one panel refresh per iteration, whatever happened
		e = sleep_until_next_cycle(next_tick, menu_active);
	}
}
