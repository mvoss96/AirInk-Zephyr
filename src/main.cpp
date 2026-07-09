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

/* Device mode. Measurement only runs in Normal; calibration/reset (later) switch
 * the mode and the button handler drives their flow. */
enum class Mode : uint8_t
{
	Normal /*, Calibration, Reset */
};

static Mode mode = Mode::Normal;
static uint16_t last_co2_ppm; // last full CO2 read, held on screen between them
static uint32_t tick_count;
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

	// The battery read above still runs, so charging is noticed within one tick.
	if (low_battery)
	{
		printk("[LOW] batt %d%%%s  measurement suspended\n",
			   batt_pct, b.charging ? " CHG" : "");
		if (batt_pct != BATT_UNKNOWN)
		{
			ui::set_battery((uint8_t)batt_pct, b.charging);
			ui::set_low_battery((uint8_t)batt_pct);
		}
		ui::refresh();
		return;
	}

	const bool full_co2 = (tick_count % CO2_EVERY_TICKS) == 0;

	// CO2 tick only: a percent step must not force a refresh on a cheap T+RH tick.
	if (full_co2 && batt_pct != BATT_UNKNOWN)
	{
		ui::set_battery((uint8_t)batt_pct, b.charging);
	}

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

/* Periodic measurement: runs on the system work queue and reschedules itself.
 * The single-shot SCD41 fetch blocks ~5 s, which is fine here. */
static void measure_work_cb(struct k_work *);
K_WORK_DELAYABLE_DEFINE(measure_work, measure_work_cb);

static void measure_work_cb(struct k_work *)
{
	if (mode == Mode::Normal)
	{
		console_uart(true);
		do_measurement();
		console_uart(false);
	}
	k_work_reschedule(&measure_work, K_MSEC(TICK_MS));
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

	// First measurement now, then every TICK_MS. The work callback wakes the console
	// around each cycle; suspend it here so the interval before the first is quiet.
	k_work_schedule(&measure_work, K_NO_WAIT);
	console_uart(false);
	return 0;
}
