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

/* Dual cadence (see docs/power-analysis.md): the CO2 single-shot costs ~70 mAs
 * (dominates the energy budget) while a T+RH-only read is ~1000x cheaper. So tick
 * every 30 s doing a cheap T+RH read, and only every CO2_EVERY_TICKS-th tick (=
 * 5 min) do the full CO2 read. */
static constexpr int TICK_MS = 30000;
static constexpr int CO2_EVERY_TICKS = 10;

/* Below this (on the primary/external channel) show the low-battery screen. */
static constexpr int LOW_BATTERY_PCT = 5;

/* Device mode. Measurement only runs in Normal; calibration/reset (later) switch
 * the mode and the button handler drives their flow. */
enum class Mode : uint8_t
{
	Normal /*, Calibration, Reset */
};
static Mode mode = Mode::Normal;
static bool display_ok;

/* Last full CO2 reading, held on screen between the 5-min CO2 reads while the
 * cheap T+RH ticks refresh temperature/humidity. */
static uint16_t last_co2_ppm;
static uint32_t tick_count;

/* One cycle: battery + SCD41 (full CO2 on every CO2_EVERY_TICKS-th tick, else
 * T+RH-only), then one display refresh. */
static void do_measurement()
{
	const bool full_co2 = (tick_count % CO2_EVERY_TICKS) == 0;

	/* Battery: sample every tick (cheap ADC; battery::sample smooths internally) but
	 * only push it to the status bar on the 5-min CO2 tick, so a percent step never
	 * forces an e-paper refresh on the cheap T+RH ticks. */
	BatteryReading b{};
	const bool batt_ok = (battery::sample(&b) == 0);
	const int batt_pct = batt_ok ? b.ext_pct : 100;

	if (full_co2 && batt_ok && display_ok)
	{
		ui::set_battery((uint8_t)batt_pct, b.charging);
	}

	Scd41Reading r{};
	const int rc = full_co2 ? scd41::sample(&r) : scd41::sample_rht(&r);
	if (rc == 0)
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
			   batt_ok ? batt_pct : -1, b.charging ? " CHG" : "");
		if (display_ok)
		{
			if (batt_ok && batt_pct <= LOW_BATTERY_PCT)
			{
				ui::set_low_battery((uint8_t)batt_pct);
			}
			else
			{
				ui::set_sensor(r.co2_ppm, r.temp_x100, r.hum_x100);
			}
		}
	}
	else
	{
		printk("SCD41: %s read failed\n", full_co2 ? "CO2" : "RHT");
		if (display_ok)
		{
			ui::set_error("SENSOR ERROR", "SCD41 read failed");
		}
	}

	/* One panel refresh for the whole cycle (battery + view). */
	if (display_ok)
	{
		ui::refresh();
	}
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
	display_ok = (ui::init() == 0);

	printk("AirInk v%s (%s %s) started (display %s)\n",
		   AIRINK_VERSION, __DATE__, __TIME__, display_ok ? "ok" : "FAILED");

	if (scd41::init() < 0)
	{
		printk("SCD41 not ready\n");
		if (display_ok)
		{
			ui::set_error("SENSOR ERROR", "SCD41 not found");
			ui::refresh();
		}
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
