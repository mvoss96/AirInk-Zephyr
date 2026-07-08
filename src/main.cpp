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
 * MEASURE_INTERVAL_MS, not a continuous stream. */
static const struct device *const console_dev =
	DEVICE_DT_GET(DT_CHOSEN(zephyr_console));

static void console_uart(bool on)
{
	pm_device_action_run(console_dev,
			     on ? PM_DEVICE_ACTION_RESUME : PM_DEVICE_ACTION_SUSPEND);
}

/* Time between single-shot measurements. Each fetch itself blocks ~5 s, and a
 * full 4.2" e-paper refresh takes a few seconds. */
static constexpr int MEASURE_INTERVAL_MS = 60000;

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

/* One measurement cycle: battery + SCD41, then update the display. */
static void do_measurement()
{
	BatteryReading b{};
	const bool batt_ok = (battery::sample(&b) == 0);
	if (batt_ok)
	{
		printk("Batt ext %d mV (%d%%)  int %d mV (%d%%)  %s\n",
			   b.ext_mv, b.ext_pct, b.int_mv, b.int_pct,
			   b.charging ? "CHARGING" : "battery");
		if (display_ok)
		{
			ui::set_battery((uint8_t)b.ext_pct, b.charging);
		}
	}

	Scd41Reading r;
	if (scd41::sample(&r) == 0)
	{
		printk("CO2 %u ppm  T %d.%02d C  RH %d.%02d %%\n", r.co2_ppm,
			   r.temp_x100 / 100, abs(r.temp_x100 % 100),
			   r.hum_x100 / 100, r.hum_x100 % 100);
		if (display_ok)
		{
			if (batt_ok && b.ext_pct <= LOW_BATTERY_PCT)
			{
				ui::set_low_battery((uint8_t)b.ext_pct);
			}
			else
			{
				ui::set_sensor(r.co2_ppm, r.temp_x100, r.hum_x100);
			}
		}
	}
	else
	{
		printk("SCD41: sample fetch failed\n");
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
	k_work_reschedule(&measure_work, K_MSEC(MEASURE_INTERVAL_MS));
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
