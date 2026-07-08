#include <stdlib.h>
#include <zephyr/kernel.h>

#include "sensors/battery.hpp"
#include "sensors/scd41.hpp"
#include "ui/display_ui.hpp"
#include "version.hpp"

/* Time between single-shot measurements. Each fetch itself blocks ~5 s, and a
 * full 4.2" e-paper refresh takes a few seconds. */
static constexpr int MEASURE_INTERVAL_MS = 60000;

/* Below this (on the primary/external channel) show the low-battery screen. */
static constexpr int LOW_BATTERY_PCT = 5;

int main(void)
{
	const bool display_ok = (ui::init() == 0);

	printk("AirInk v%s (%s %s) started (display %s)\n",
	       AIRINK_VERSION, __DATE__, __TIME__, display_ok ? "ok" : "FAILED");

	if (scd41::init() < 0)
	{
		printk("SCD41 not ready\n");
		if (display_ok)
		{
			ui::show_error("SENSOR ERROR", "SCD41 not found");
		}
		return 0; /* leave the error on screen */
	}

	if (battery::init() < 0)
	{
		printk("Battery ADC not ready (continuing without it)\n");
	}

	while (true)
	{
		/* Battery: measure both channels, log both, feed the status bar from the
		 * primary (external P0.31) channel. */
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
			printk("CO2 %u ppm  T %d.%02d C  RH %d.%02d %%\n", r.co2_ppm, r.temp_x100 / 100, abs(r.temp_x100 % 100), r.hum_x100 / 100, r.hum_x100 % 100);

			if (display_ok)
			{
				if (batt_ok && b.ext_pct <= LOW_BATTERY_PCT)
				{
					ui::show_low_battery((uint8_t)b.ext_pct);
				}
				else
				{
					ui::show_reading(r.co2_ppm, r.temp_x100, r.hum_x100);
				}
			}
		}
		else
		{
			printk("SCD41: sample fetch failed\n");
			if (display_ok)
			{
				ui::show_error("SENSOR ERROR", "SCD41 read failed");
			}
		}

		k_msleep(MEASURE_INTERVAL_MS);
	}

	return 0;
}
