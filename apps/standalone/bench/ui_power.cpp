/** @file
 * What the interactive screens cost -- firmware that REPLACES apps/standalone/main.cpp.
 *
 * The button cannot be pressed by a script, so this harness walks into the states
 * directly. Measure on the PPK2; results belong in docs/power-analysis.md.
 *
 *   west build -b promicro_nrf52840/nrf52840 apps/standalone -p always -- -DAPP_ENTRY=ui_power
 *
 * MODE_CALIB deliberately does NOT send the forced recalibration. Running an FRC in
 * indoor air would teach the sensor that ~800 ppm is fresh outdoor air, permanently.
 * The energy question is about the three minutes of periodic measurement and the
 * countdown refreshes, and those it reproduces exactly.
 */
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/pm/device.h>
#include <stdio.h>

#include "sensors/scd41.hpp"
#include "ui/display_ui.hpp"

// ---- pick the test (change per build) ----
#define MODE_MENU_IDLE 1 // menu on screen, nothing happening: is anything left awake?
#define MODE_MENU_NAV  2 // a cursor move every 3 s: what one partial refresh costs
#define MODE_CALIB     3 // the real 3 min of periodic measurement, minus the FRC
#define TEST_MODE      MODE_CALIB

static const struct device *const console_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));

/** Mirror the firmware's cadence: the bar advances with the sensor's 5 s interval. */
static constexpr int64_t CALIB_DRAW_MS = 5000;
static constexpr int64_t CALIB_MS = 180000;

int main(void)
{
	printk("ui-power: mode %d start\n", TEST_MODE);

	if (ui::init() != 0)
	{
		printk("display not ready\n");
		return 0;
	}
	ui::set_battery(50, false);
	ui::refresh();

#if TEST_MODE == MODE_MENU_IDLE
	ui::set_menu(ui::Menu::Calibrate);
	ui::refresh();
	printk("menu shown, suspending console, sleeping\n");
	k_msleep(200);
	pm_device_action_run(console_dev, PM_DEVICE_ACTION_SUSPEND);
	while (1)
	{
		k_msleep(60000);
	}

#elif TEST_MODE == MODE_MENU_NAV
	printk("cursor moves every 3 s\n");
	k_msleep(200);
	int i = 0;
	while (1)
	{
		pm_device_action_run(console_dev, PM_DEVICE_ACTION_RESUME);
		ui::set_menu((ui::Menu)(i++ % (int)ui::Menu::Count));
		ui::refresh();
		pm_device_action_run(console_dev, PM_DEVICE_ACTION_SUSPEND);
		k_msleep(3000);
	}

#else // MODE_CALIB
	if (scd41::init() < 0)
	{
		printk("SCD41 not ready\n");
		return 0;
	}
	if (scd41::calibrate_begin() < 0)
	{
		printk("could not start periodic measurement\n");
		return 0;
	}
	printk("periodic measurement running for %lld s\n", CALIB_MS / 1000);

	const int64_t end_at = k_uptime_get() + CALIB_MS;
	int64_t next_draw = k_uptime_get();
	while (k_uptime_get() < end_at)
	{
		const int64_t done = CALIB_MS - (end_at - k_uptime_get());
		ui::set_calib_progress((uint8_t)(done * 100 / CALIB_MS));
		ui::refresh();
		pm_device_action_run(console_dev, PM_DEVICE_ACTION_SUSPEND);
		next_draw += CALIB_DRAW_MS;
		k_sleep(K_TIMEOUT_ABS_MS(next_draw));
		pm_device_action_run(console_dev, PM_DEVICE_ACTION_RESUME);
	}

	// The FRC would go here. It is left out on purpose -- see the file comment.
	if (scd41::calibrate_abort() < 0)
	{
		printk("abort failed\n");
	}
	printk("calibration window done, back to single-shot; sleeping\n");
	k_msleep(200);
	pm_device_action_run(console_dev, PM_DEVICE_ACTION_SUSPEND);
	while (1)
	{
		k_msleep(60000);
	}
#endif
	return 0;
}
