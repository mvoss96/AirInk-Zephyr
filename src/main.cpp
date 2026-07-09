#include <stdlib.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/pm/device.h>

#include "sensors/battery.hpp"
#include "sensors/scd41.hpp"
#include "ui/display_ui.hpp"
#include "version.hpp"

static const struct device *const console_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
static void console_uart(bool on)
{
	pm_device_action_run(console_dev, on ? PM_DEVICE_ACTION_RESUME : PM_DEVICE_ACTION_SUSPEND);
}

static constexpr int TICK_MS = 30000;			// 30 s between each measurement cycle (5 min CO2 + 30 s T+RH)
static constexpr int CO2_EVERY_TICKS = 10;		// full CO2 read every 10 ticks (5 min)
static constexpr int BATT_UNKNOWN = -1;			// battery percent when the ADC read failed
static constexpr int LOW_BATTERY_ENTER_PCT = 5; // battery percent at which the low-battery latch engages
static constexpr int LOW_BATTERY_EXIT_PCT = 8;	// battery percent at which the low-battery latch disengages

static uint16_t last_co2_ppm; // last full CO2 read, held on screen between them
static uint32_t tick_count;	  // number of measurement cycles since boot
static bool low_battery;	  // latched; while set the SCD41 is not read at all

/*
 * One cycle: battery, then -- unless the battery is low -- the SCD41 (full CO2 on
 * every CO2_EVERY_TICKS-th tick, else T+RH-only), then one display refresh.
 *
 * The low-battery latch uses hysteresis rather than a plain threshold: the smoothed
 * percent drifts across a single boundary, and each flap would be a view change, i.e.
 * a full-refresh black flash. A CO2 single-shot is ~70 mAs (~86 % of the budget), so
 * suspending it lets the last few percent coast at the ~60 uA idle while the panel
 * holds the warning without power. Charging always releases the latch.
 */
static void do_measurement()
{
	BatteryReading b{};
	const int batt_pct = (battery::sample(&b) == 0) ? b.ext_pct : BATT_UNKNOWN;

	if (batt_pct != BATT_UNKNOWN) // a failed read leaves the latch as it was
	{
		if (b.charging || batt_pct >= LOW_BATTERY_EXIT_PCT)
		{
			if (low_battery)
			{
				tick_count = 0; // resume on a full CO2 read; last_co2_ppm is stale
			}
			low_battery = false;
		}
		else if (batt_pct <= LOW_BATTERY_ENTER_PCT)
		{
			low_battery = true;
		}
	}

	if (batt_pct != BATT_UNKNOWN)
	{
		ui::set_battery((uint8_t)batt_pct, b.charging);
	}

	// The battery read above still runs, so charging is noticed within one tick.
	if (low_battery)
	{
		printk("[LOW] batt %d%%%s  measurement suspended\n",
			   batt_pct, b.charging ? " CHG" : "");
		ui::set_low_battery();
		ui::refresh();
		return;
	}

	const bool full_co2 = (tick_count % CO2_EVERY_TICKS) == 0;

	Scd41Reading r{};
	if ((full_co2 ? scd41::sample(&r) : scd41::sample_rht(&r)) != 0)
	{
		printk("SCD41: %s read failed\n", full_co2 ? "CO2" : "RHT");
		ui::set_error("SENSOR ERROR", "SCD41 read failed");
	}
	else
	{
		if (full_co2)
		{
			last_co2_ppm = r.co2_ppm;
		}
		else
		{
			r.co2_ppm = last_co2_ppm; // keep the last CO2 shown
		}

		printk("%s CO2 %u ppm  T %d.%02d C  RH %d.%02d %%  batt %d%%%s\n",
			   full_co2 ? "[CO2]" : "[RHT]", r.co2_ppm,
			   r.temp_x100 / 100, abs(r.temp_x100 % 100),
			   r.hum_x100 / 100, r.hum_x100 % 100,
			   batt_pct, b.charging ? " CHG" : "");

		ui::set_sensor(r.co2_ppm, r.temp_x100, r.hum_x100);
	}

	ui::refresh(); // one panel refresh for the whole cycle (battery + view)
	tick_count++;
}

int main(void)
{
	const bool display_ok = (ui::init() == 0); // on failure the ui:: API is a safe no-op

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

	/*
	 * Measurement loop. It runs on main's own stack, not the system work queue: a CO2
	 * single-shot blocks for ~5 s, and the system queue is shared infrastructure that
	 * Zephyr subsystems -- the BLE stack, once the radio lands -- also submit to.
	 *
	 * The console is woken only around each cycle; between them the UARTE is suspended
	 * so the HFCLK can stop (that is the difference between ~1 mA and ~60 uA idle).
	 */
	while (true)
	{
		console_uart(true);
		do_measurement();
		console_uart(false);
		k_sleep(K_MSEC(TICK_MS));
	}
}
