#include <stdlib.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/pm/device.h>

#include "sensors/battery.hpp"
#include "sensors/scd41.hpp"
#include "ui/display_ui.hpp"
#include "version.hpp"

/* Console UART (uart0). Left enabled it keeps the UARTE peripheral + HFCLK/HFXO
 * running continuously (~1 mA idle). We deep-suspend it between measurements and
 * only wake it to emit the periodic log lines — so COM9 sees a burst every
 * TICK_MS, not a continuous stream. */
static const struct device *const console_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
static void console_uart(bool on)
{
	pm_device_action_run(console_dev, on ? PM_DEVICE_ACTION_RESUME : PM_DEVICE_ACTION_SUSPEND);
}

static constexpr int TICK_MS = 30000;	   // 30 s between each measurement cycle (5 min CO2 + 30 s T+RH)
static constexpr int CO2_EVERY_TICKS = 10; // full CO2 read every 10 ticks (5 min)
static constexpr int BATT_UNKNOWN = -1;	   // battery percent when the ADC read failed

/* Low-battery hysteresis: enter at or below ENTER, leave only at or above EXIT (or
 * once charging). The gap matters -- a single threshold would flap as the smoothed
 * percent drifts across it, and each flap is a view change, i.e. a full e-paper
 * refresh with its black flash. */
static constexpr int LOW_BATTERY_ENTER_PCT = 5;
static constexpr int LOW_BATTERY_EXIT_PCT = 8;

/* Device mode. Measurement only runs in Normal; calibration/reset (later) switch
 * the mode and the button handler drives their flow. */
enum class Mode : uint8_t
{
	Normal /*, Calibration, Reset */
};

static Mode mode = Mode::Normal;
static uint16_t last_co2_ppm;
static uint32_t tick_count;

/* Latched: while set, the SCD41 is not read at all (a CO2 single-shot is ~70 mAs,
 * ~86 % of the energy budget). The panel keeps the low-battery screen without power,
 * so the warning stays visible while the device coasts at its ~60 uA idle. */
static bool low_battery;

/* One cycle: battery, then (unless the battery is low) SCD41 -- full CO2 on every
 * CO2_EVERY_TICKS-th tick, else T+RH-only -- then one display refresh. */
static void do_measurement()
{
	/* Battery: sample every tick (cheap ADC, and the EMA wants that cadence).
	 * BATT_UNKNOWN is the single encoding of "not read". */
	BatteryReading b{};
	const int batt_pct = (battery::sample(&b) == 0) ? b.ext_pct : BATT_UNKNOWN;

	/* Update the latch. A failed read leaves the state as it was -- one bad ADC
	 * sample must not resume the sensor on an empty cell, nor suspend it on a full
	 * one. Charging always releases the latch: on USB, power is no longer scarce. */
	if (batt_pct != BATT_UNKNOWN)
	{
		if (b.charging || batt_pct >= LOW_BATTERY_EXIT_PCT)
		{
			if (low_battery)
			{
				/* Resume on a full CO2 read: last_co2_ppm is stale, and if we booted
				 * straight into low battery it was never read at all. */
				tick_count = 0;
			}
			low_battery = false;
		}
		else if (batt_pct <= LOW_BATTERY_ENTER_PCT)
		{
			low_battery = true;
		}
	}

	/* Low battery: stop measuring entirely and just keep the warning on screen. The
	 * battery read above is all that still runs, so charging is noticed within a tick.
	 * The setters dedup, so a stable percent costs no refresh at all. tick_count does
	 * not advance -- the CO2 cadence resumes from a clean phase above. */
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

	/* Stage the status bar only on the CO2 tick, so a percent step never forces an
	 * e-paper refresh on the cheap T+RH ticks. */
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
			r.co2_ppm = last_co2_ppm; /* keep the last CO2 shown */
		}

		printk("%s CO2 %u ppm  T %d.%02d C  RH %d.%02d %%  batt %d%%%s\n",
			   full_co2 ? "[CO2]" : "[RHT]", r.co2_ppm,
			   r.temp_x100 / 100, abs(r.temp_x100 % 100),
			   r.hum_x100 / 100, r.hum_x100 % 100,
			   batt_pct, b.charging ? " CHG" : "");

		ui::set_sensor(r.co2_ppm, r.temp_x100, r.hum_x100);
	}

	/* One panel refresh for the whole cycle (battery + view). */
	ui::refresh();
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
		console_uart(true); /* wake the console just for the log lines */
		do_measurement();
		console_uart(false); /* back to deep sleep for the idle interval */
	}
	k_work_reschedule(&measure_work, K_MSEC(TICK_MS));
}

int main(void)
{
	/* On failure the whole ui:: API becomes a no-op, so nothing below needs to guard
	 * its calls; we only report it. */
	const bool display_ok = (ui::init() == 0);

	printk("AirInk v%s (%s %s) started (display %s)\n",
		   AIRINK_VERSION, __DATE__, __TIME__, display_ok ? "ok" : "FAILED");

	if (scd41::init() < 0)
	{
		printk("SCD41 not ready\n");
		ui::set_error("SENSOR ERROR", "SCD41 not found");
		ui::refresh();
		return 0; /* leave the error on screen */
	}
	if (battery::init() < 0)
	{
		printk("Battery ADC not ready (continuing without it)\n");
	}

	/* First measurement now, then every MEASURE_INTERVAL_MS. The work callback
	 * wakes the console around each cycle; suspend it here so the interval before
	 * that first cycle is already quiet. */
	k_work_schedule(&measure_work, K_NO_WAIT);
	console_uart(false);
	return 0;
}
