#include "app.hpp"

#include <stdlib.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/pm/device.h>

#include "input/button.hpp"
#include "menu.hpp"
#include "version.hpp"

static const struct device *const console_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));

static constexpr int TICK_MS = 30000;			// How often a measurement cycle runs
static constexpr uint32_t CO2_EVERY_TICKS = 10; // Every tenth cycle is a full CO2 read; the others are T+RH only
static uint32_t tick_count;						// cycles since boot, or since the last recalibration
static uint16_t last_co2_ppm;					// held on screen between the five-minute CO2 reads

static app::Hooks hooks;

// Not copied: they are string literals or static buffers owned by the caller (see app.hpp).
static const char *build_name = "Standalone";
static const char *pair_qr;
static const char *pair_manual;

// Written by any thread, read by the loop. A plain enum store is atomic on this core, and
// a stale read costs nothing worse than one cycle of a stale indicator.
static ui::Link link_state = ui::Link::None;
static bool is_commissioned;

void app::set_hooks(const Hooks &h)
{
	hooks = h;
}

void app::set_build_name(const char *name)
{
	build_name = name;
}

void app::set_pairing_codes(const char *qr, const char *manual)
{
	pair_qr = qr;
	pair_manual = manual;
}

void app::set_commissioned(bool on_fabric)
{
	is_commissioned = on_fabric;
}

bool app::commissioned()
{
	return is_commissioned;
}

void app::set_link(ui::Link state)
{
	link_state = state;
}

/** Read the SCD41 and put the numbers on screen.
 *
 * A full CO2 single-shot on every tenth call, a ~1000x cheaper T+RH read otherwise; the
 * last CO2 value stays on screen in between. A reading also selects the sensor view,
 * which is how the boot splash and a stale error message go away.
 *
 * On a read error the error view is staged instead, and the cadence does not advance --
 * so the next call retries the same kind of read.
 */
static void measure()
{
	const bool full_co2 = (tick_count % CO2_EVERY_TICKS) == 0;

	Scd41Reading r{};
	if ((full_co2 ? scd41::sample(&r) : scd41::sample_rht(&r)) != 0)
	{
		printk("SCD41: %s read failed\n", full_co2 ? "CO2" : "RHT");
		ui::set_error("SENSOR ERROR", "SCD41 read failed");
		return;
	}

	if (full_co2)
	{
		last_co2_ppm = r.co2_ppm;
	}
	else
	{
		r.co2_ppm = last_co2_ppm;
	}

	printk("%s CO2 %u ppm  T %d.%02d C  RH %d.%02d %%\n",
		   full_co2 ? "[CO2]" : "[RHT]", r.co2_ppm,
		   r.temp_x100 / 100, abs(r.temp_x100 % 100),
		   r.hum_x100 / 100, r.hum_x100 % 100);

	ui::show_sensor();
	ui::set_sensor(r.co2_ppm, r.temp_x100, r.hum_x100);

	// Only a cycle that actually smelled the air is worth forwarding: on the nine cycles
	// in between, r.co2_ppm is the retained value, and republishing it would claim a
	// freshness it does not have.
	if (full_co2 && hooks.reading)
	{
		hooks.reading(r);
	}

	tick_count++;
}

/** The main app loop */
static void app_loop()
{
	int64_t next_measure = k_uptime_get(); // the first one is due at once
	bool menu_active = false;
	button::Event e = button::Event::None;

	while (true)
	{
		const int64_t now = k_uptime_get();
		const battery::State bat = battery::read();
		ui::set_battery(bat.pct, bat.charging);
		ui::set_link(link_state);

		if (hooks.battery)
		{
			hooks.battery(bat);
		}

		if (bat.low)
		{
			if (menu_active)
			{
				menu::abort(); // a calibration would leave the SCD41 in periodic mode
				menu_active = false;
			}
			printk("[LOW] batt %u%%%s  measurement suspended\n", bat.pct, bat.charging ? " CHG" : "");
			ui::set_low_battery();
			next_measure = now + TICK_MS;
		}
		else if (menu_active)
		{
			const menu::Status status = menu::proceed(e);
			if (status != menu::Status::Running)
			{
				if (status == menu::Status::Recalibrated)
				{
					// The retained CO2 value predates the correction, so the next read must be a full
					tick_count = 0;
				}
				else if (status == menu::Status::FactoryReset && hooks.factory_reset)
				{
					// Leaves the screen as it is: the hook reboots us, and a device that is
					// about to lose its network should not spend its last second drawing.
					printk("[UI] factory reset\n");
					hooks.factory_reset();
				}
				else
				{
					ui::show_sensor();
				}
				menu_active = false;
				next_measure = now;
			}
		}
		else if (e == button::Event::Long)
		{
			menu_active = true;
			menu::enter();
		}
		else if (now >= next_measure)
		{
			measure();
			next_measure = now + TICK_MS;
		}

		ui::refresh(); // exactly one panel refresh per iteration, whatever happened

		// Whichever mode we are in has exactly one deadline. A deadline already in the past
		// makes wait_until() return at once, which is how leaving the menu gets its reading
		// without a second code path; the iteration that follows always pushes next_measure,
		// so this cannot spin.
		const int64_t wake_at = menu_active ? menu::deadline_ms() : next_measure;

#ifndef CONFIG_CHIP
		// A live console UARTE keeps the HFCLK running: ~1 mA versus the ~60 uA idle floor.
		// Not in the Matter build: there the console is not ours to switch off -- the CHIP
		// and OpenThread threads log while we wait here, and the radio dominates the budget
		// anyway. Power for that build is its own exercise.
		pm_device_action_run(console_dev, PM_DEVICE_ACTION_SUSPEND);
#endif
		e = button::wait_until(wake_at);
#ifndef CONFIG_CHIP
		pm_device_action_run(console_dev, PM_DEVICE_ACTION_RESUME);
#endif

		if (e != button::Event::None)
		{
			printk("[BTN] %s\n", (e == button::Event::Long) ? "hold" : "tap");
		}
	}
}

void app::run()
{
	// What the UI shows and offers is decided here, by what the caller actually gave us: a Matter
	// row where there are codes to show, a Factory reset row where there is something to reset.
	const ui::Config ui_cfg = {
		.build = build_name,
		.pair_qr = pair_qr,
		.pair_manual = pair_manual,
		.factory_reset = hooks.factory_reset != nullptr,
	};
	const bool display_ok = (ui::init(ui_cfg) == 0);

	printk("AirInk v%s %s (%s %s) started (display %s)\n",
		   AIRINK_VERSION, build_name, __DATE__, __TIME__, display_ok ? "ok" : "FAILED");

	if (scd41::init() < 0)
	{
		printk("SCD41 not ready\n");
		ui::set_error("SENSOR ERROR", "SCD41 not found");
		ui::refresh();
		k_sleep(K_FOREVER); // leave the error on screen
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

	app_loop();
}
