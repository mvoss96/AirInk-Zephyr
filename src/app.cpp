#include "app.hpp"

#include <stdlib.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>

#include "input/button.hpp"
#include "menu.hpp"
#include "prefs.hpp"
#include "version.hpp"

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

void app::open_pairing()
{
	if (hooks.pairing_open)
	{
		hooks.pairing_open();
	}
}

void app::publish_unit(ui::TempUnit u)
{
	if (hooks.publish_unit)
	{
		hooks.publish_unit(u);
	}
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

/* Raised from the POWER interrupt when the USB cable moves. */
static K_SEM_DEFINE(supply_changed, 0, 1);

static void on_supply_change()
{
	k_sem_give(&supply_changed);
}

/** Sleep until something worth redrawing happens, or until the deadline.
 *
 * Two things count as something: the button, and the USB cable. The cable matters because the
 * charging bolt is on screen, and a bolt that takes half a minute to go out after the cable is
 * pulled reads as a bug, not as a cadence -- charging is detected on the raw voltages, so it was
 * never stale; the panel simply had nobody to tell it.
 *
 * Polling for it would have cost current every cycle for an event that happens twice a week. The
 * SoC raises USBDETECTED/USBREMOVED on its own, so we wait for it instead, and the caller does
 * what it does anyway on every pass: read the battery, stage it, refresh. An early wake needs no
 * special case -- it just makes the next pass happen sooner.
 *
 * @param deadline_ms absolute k_uptime_get() value; already past means "just take what is queued"
 * @return the gesture, or Event::None -- which is also what a cable wake looks like
 */
static button::Event wait(int64_t deadline_ms)
{
	const int64_t now = k_uptime_get();
	if (deadline_ms > now)
	{
		struct k_poll_event ev[2];
		k_poll_event_init(&ev[0], K_POLL_TYPE_MSGQ_DATA_AVAILABLE, K_POLL_MODE_NOTIFY_ONLY,
						  button::queue());
		k_poll_event_init(&ev[1], K_POLL_TYPE_SEM_AVAILABLE, K_POLL_MODE_NOTIFY_ONLY,
						  &supply_changed);
		k_poll(ev, ARRAY_SIZE(ev), K_MSEC(deadline_ms - now));
	}

	// The cable wake carries no information beyond "look again", and looking again is what the
	// caller is about to do. Consume it so it cannot wake us a second time.
	k_sem_take(&supply_changed, K_NO_WAIT);

	return button::wait_until(0); // a gesture if k_poll found one; None otherwise
}

/** Read the battery, put it on the status bar, and tell whoever is listening.
 *
 * Every pass of every loop starts here, so this is also where the Matter build refreshes its view of
 * the fabric table (it rides along in the battery hook) -- which is how the onboarding screen below
 * notices somebody commissioning us without a gesture.
 *
 * @return what the battery is doing, for the caller that has to act on it
 */
static battery::State poll_battery()
{
	const battery::State bat = battery::read();
	ui::set_battery(bat.pct, bat.charging);
	ui::set_link(link_state);

	if (hooks.battery)
	{
		hooks.battery(bat);
	}
	return bat;
}

/** A device that has never joined anything boots to its own onboarding code.
 *
 * The readings are not what a factory-new device is for: nobody has told it where to send them, and
 * the one thing the user has to do -- scan the code -- is the one thing the panel can help with. So
 * it shows the code first and the numbers afterwards. The radio is already listening; the boot
 * window opened on its own (CONFIG_CHIP_ENABLE_PAIRING_AUTOSTART).
 *
 * Nothing is measured while this is up. That is not laziness: a reading here would have nowhere to
 * go, and staging it would repaint the panel -- and a panel refresh is the single most expensive
 * thing this device does (~39 % of its energy). The screen is e-paper; leaving the code on it costs
 * nothing at all.
 *
 * Two ways out, and the device takes whichever comes first:
 *   - a fabric appears  -> somebody scanned it. On to the readings, no gesture needed.
 *   - the button        -> the user declined. On to the readings, and the caller is told, so it can
 *                          cut the commissioning window short rather than leave it open for the
 *                          hour boot gave it.
 *
 * A build with no codes -- the standalone one -- has no onboarding to do and returns at once.
 */
static void onboarding()
{
	if (!pair_qr || is_commissioned)
	{
		return;
	}

	printk("[MTR] not commissioned; the panel shows the onboarding code\n");
	ui::show_matter(false);

	while (true)
	{
		poll_battery(); // also refreshes is_commissioned, which is the way out below

		if (is_commissioned)
		{
			printk("[MTR] commissioned; on to the readings\n");
			return;
		}

		ui::refresh(); // only actually repaints when the battery moved

		if (wait(k_uptime_get() + TICK_MS) != button::Event::None)
		{
			printk("[MTR] onboarding code dismissed\n");
			if (hooks.pairing_dismissed)
			{
				hooks.pairing_dismissed();
			}
			return;
		}
	}
}

/** Adopt a temperature unit the controller set, if it did.
 *
 * The other half of the menu's toggle. A user with the phone in their hand may well change the unit
 * from Home Assistant rather than walk to the device, and the panel has to agree with what the app
 * shows -- a thermometer that disagrees with its own record is worse than one with no app at all.
 *
 * It is a poll and not a callback because the cluster gives us nothing to hang a callback on; see
 * Hooks::unit_from_network. Once a tick is not a compromise here: the e-paper cannot show it sooner.
 *
 * Whatever comes in goes to prefs as well, so the device boots on the last thing anyone chose --
 * whoever chose it, and wherever.
 */
static void pull_unit()
{
	if (!hooks.unit_from_network)
	{
		return; // no network, no second opinion
	}

	ui::TempUnit u;
	if (!hooks.unit_from_network(&u) || u == ui::temp_unit_shown())
	{
		return;
	}

	printk("[PREFS] temp unit %s (from the network)\n", u == ui::TempUnit::Fahrenheit ? "F" : "C");
	prefs::set_temp_unit(u);
	ui::set_temp_unit(u);
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
		pull_unit();

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
	// Before the panel, because ui::init() builds the sensor view and the menu with a unit already in
	// them. A store that will not come up is survivable: prefs answers Celsius and says so in the log.
	prefs::init();

	const bool display_ok = (ui::init(ui_cfg) == 0);

	// init() shows the splash, not the readings, so this lands before the user could see the wrong
	// unit -- and it costs no extra refresh.
	ui::set_temp_unit(prefs::temp_unit());

	// ...and outward, before the loop ever polls the other way. The Matter cluster reloads a value of
	// its own on boot, so if we only ever listened, its copy would win every restart and the unit the
	// user set on the panel would not survive one. Pushing first makes prefs the authority and the
	// cluster the mirror; from here on they only ever move together.
	if (hooks.publish_unit)
	{
		hooks.publish_unit(prefs::temp_unit());
	}

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
	else if (battery::watch_supply(on_supply_change) < 0)
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
