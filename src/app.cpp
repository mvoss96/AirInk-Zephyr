#include "app.hpp"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>

#include "input/button.hpp"
#include "menu.hpp"
#include "net.hpp"
#include "prefs.hpp"
#include "sensors/battery.hpp"
#include "sensors/scd41.hpp"
#include "ui/display_ui.hpp"
#include "version.hpp"

static constexpr int TICK_MS = 30000;			// How often a measurement cycle runs
static constexpr uint32_t CO2_EVERY_TICKS = 10; // Every tenth cycle is a full CO2 read; the others are T+RH only
static uint32_t tick_count;						// cycles since boot, or since the last recalibration

/** Read the SCD41 and put the numbers on screen. Every tenth call is the full CO2 single-shot,
 * the rest are ~1000x cheaper T+RH reads (the sensor module carries the CO2 value across them).
 * A reading also selects the sensor view, which is how the splash and a stale error go away. On a
 * read error the cadence does not advance, so the next call retries the same kind of read. */
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

	// %d.%02d with abs() on the fraction drops the sign for -1.00 < T < 0.00 -- integer division
	// truncates toward zero, so -0.5 C prints as "0.5". The sign goes in front of the whole thing.
	const int32_t t_abs = (r.temp_x100 < 0) ? -r.temp_x100 : r.temp_x100;
	printk("%s CO2 %u ppm  T %s%d.%02d C  RH %d.%02d %%\n", full_co2 ? "[CO2]" : "[RHT]", r.co2_ppm,
		   (r.temp_x100 < 0) ? "-" : "", t_abs / 100, t_abs % 100, r.hum_x100 / 100, r.hum_x100 % 100);

	ui::show_sensor();
	ui::set_sensor(r.co2_ppm, r.temp_x100, r.hum_x100);

	// The network hears about it when there is something to say -- which is net's judgement, not
	// ours (see net::publish_reading): a fresh CO2 number always goes, a temperature goes when it
	// moved by more than the panel could show.
	net::publish_reading(r, full_co2);

	tick_count++;
}

/* Given from the POWER interrupt when the USB cable moves (see battery::watch_supply). */
static K_SEM_DEFINE(supply_changed, 0, 1);

/** Sleep until the button, the USB cable, or the deadline. The cable wakes us so the charging
 * bolt tracks it immediately instead of lagging a 30 s cycle; an early wake needs no special case
 * because every pass re-reads the battery anyway.
 *
 * @param deadline_ms absolute k_uptime_get() value; already past = just take what is queued
 * @return the gesture, or Event::None -- which is also what a cable wake looks like
 */
static button::Event wait(int64_t deadline_ms)
{
	const int64_t now = k_uptime_get();
	if (deadline_ms > now)
	{
		struct k_poll_event ev[2];
		k_poll_event_init(&ev[0], K_POLL_TYPE_MSGQ_DATA_AVAILABLE, K_POLL_MODE_NOTIFY_ONLY, button::queue());
		k_poll_event_init(&ev[1], K_POLL_TYPE_SEM_AVAILABLE, K_POLL_MODE_NOTIFY_ONLY, &supply_changed);
		k_poll(ev, ARRAY_SIZE(ev), K_MSEC(deadline_ms - now));
	}

	// The cable wake carries no information beyond "look again", and looking again is what the
	// caller is about to do. Consume it so it cannot wake us a second time.
	k_sem_take(&supply_changed, K_NO_WAIT);

	return button::wait_until(0); // a gesture if k_poll found one; None otherwise
}

/** Read the battery, stage it, tell the network. Every pass of every loop starts here. */
static battery::State poll_battery()
{
	const battery::State bat = battery::read();
	ui::set_battery(bat.pct, bat.charging);

	net::publish_battery(bat);
	return bat;
}

/** Adopt a unit the controller set. The loop is the bridge between net and prefs, so the two
 * never call each other; prefs dedups, logs, and never echoes back to the network (adopt). */
static void pull_unit()
{
	ui::TempUnit u;
	if (net::unit_from_network(&u))
	{
		prefs::adopt(prefs::Unit, (int32_t)u);
	}
}

/** A device that has never joined anything boots to its onboarding code; the radio is already
 * listening (CONFIG_CHIP_ENABLE_PAIRING_AUTOSTART). Nothing is measured meanwhile: a reading would
 * have nowhere to go and would repaint the panel for nothing. Two ways out, whichever comes first:
 * a fabric appears (scanned), or the button (declined -- then net cuts the boot window short).
 * A build with no codes returns at once. */
static void onboarding()
{
	if (!net::has_radio() || net::commissioned())
	{
		return;
	}

	printk("[MTR] not commissioned; the panel shows the onboarding code\n");
	ui::show_matter(false);

	while (true)
	{
		poll_battery(); // the status bar stays honest while the code is up

		// Kept current by the network's own threads (a fabric delegate on the Matter build);
		// this loop only has to keep looking.
		if (net::commissioned())
		{
			printk("[MTR] commissioned; on to the readings\n");
			return;
		}

		ui::refresh(); // only actually repaints when the battery moved

		if (wait(k_uptime_get() + TICK_MS) != button::Event::None)
		{
			printk("[MTR] onboarding code dismissed\n");
			net::dismiss_pairing();
			return;
		}
	}
}

/** The main app loop */
static void app_loop()
{
	int64_t next_measure = k_uptime_get(); // the first one is due at once
	bool menu_active = false;
	bool was_low = false; // so the low-battery state is announced once, not every 30 s
	button::Event e = button::Event::None;

	while (true)
	{
		const int64_t now = k_uptime_get();
		const battery::State bat = poll_battery();
		net::poll_signal(); // what the network has to say back...
		pull_unit();		// ...and what it wants the panel to show

		if (bat.low)
		{
			if (menu_active)
			{
				menu::abort(); // a calibration would leave the SCD41 in periodic mode
				menu_active = false;
			}
			if (!was_low)
			{
				printk("[LOW] batt %u%%%s  measurement suspended\n", bat.pct,
					   bat.charging ? " CHG" : "");
			}
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
					// Restart the cadence: the recalibration disowned the sensor's held CO2 value
					// (scd41 cleared it), so the very next read must be the full one.
					tick_count = 0;
				}
				else if (status == menu::Status::FactoryReset && net::can_factory_reset())
				{
					// Leaves the screen as it is: the reset reboots us, and a device that is
					// about to lose its network should not spend its last second drawing.
					printk("[UI] factory reset\n");
					net::factory_reset();
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

		was_low = bat.low;

		// The one place the panel is committed. It paints only when a *displayed* value changed --
		// set_sensor() and set_battery() dedup on what the screen actually shows -- so most passes
		// end here without touching the e-paper. That is deliberate and it is load-bearing: a
		// refresh costs ~3 mAs, and a still room gets one per five minutes rather than ten.
		ui::refresh();

		// Whichever mode we are in has exactly one deadline. A deadline already in the past
		// makes wait() return at once, which is how leaving the menu gets its reading
		// without a second code path; the iteration that follows always pushes next_measure,
		// so this cannot spin.
		const int64_t wake_at = menu_active ? menu::deadline_ms() : next_measure;

		e = wait(wake_at);

		if (e != button::Event::None)
		{
			printk("[BTN] %s\n", (e == button::Event::Long) ? "hold" : "tap");
		}
	}
}

void app::run(const char *build_name)
{
	// What the panel needs. What the MENU offers is decided elsewhere -- it asks net::has_radio()
	// and net::can_factory_reset() -- because a row exists when there is something behind it.
	const ui::Config ui_cfg = {
		.build = build_name,
		.pair_qr = net::pair_qr(),
		.pair_manual = net::pair_manual(),
	};
	// Before the panel, because ui::init() builds the sensor view and the menu with a unit already in
	// them. A store that will not come up is survivable: prefs answers Celsius and says so in the log.
	prefs::init();

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

	/* Everything the user ever chose, put where it acts (panel, sensor, network) -- the first moment
	 * it can be, since neither existed until now; the splash is still up, so nobody sees the wrong
	 * unit. Pushing the unit OUT before the loop polls the other way is what makes prefs the
	 * authority (see net::Hooks::publish_unit). Survivable if the sensor refuses: it measures on
	 * with the factory's idea of the room. */
	prefs::apply_all();

	if (battery::init() < 0)
	{
		printk("Battery ADC not ready (continuing without it)\n");
	}
	else if (battery::watch_supply(&supply_changed) < 0)
	{
		// Survivable: the bolt then appears and clears on the next cycle, up to 30 s late,
		// which is how it behaved before this existed.
		printk("Battery: no VBUS events; the charging bolt will lag by a cycle\n");
	}
	if (button::init() < 0)
	{
		printk("Button not ready (continuing without it)\n");
	}
	ui::log_pool("boot splash"); // every view is built; this is the resident cost

	onboarding(); // returns at once unless this is a radio build that has never been commissioned
	app_loop();
}
