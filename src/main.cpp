#include <stdlib.h>
#include <zephyr/kernel.h>

#include "sensors/scd41.hpp"
#include "ui/display_ui.hpp"
#include "version.hpp"

/* Time between single-shot measurements. Each fetch itself blocks ~5 s, and a
 * full 4.2" e-paper refresh takes a few seconds */
#define MEASURE_INTERVAL_MS 60000

static Scd41 sensor;

int main(void)
{
	const bool display_ok = (ui::init() == 0);

	printk("AirInk v%s (%s %s) started (display %s)\n",
	       AIRINK_VERSION, __DATE__, __TIME__, display_ok ? "ok" : "FAILED");

	if (sensor.init() < 0)
	{
		printk("SCD41 not ready\n");
		if (display_ok)
		{
			ui::show_error("SENSOR ERROR", "SCD41 not found");
		}
		return 0; /* leave the error on screen */
	}

	while (true)
	{
		Scd41Reading r;

		if (sensor.sample(&r) == 0)
		{
			printk("CO2 %u ppm  T %d.%02d C  RH %d.%02d %%\n", r.co2_ppm, r.temp_x100 / 100, abs(r.temp_x100 % 100), r.hum_x100 / 100, r.hum_x100 % 100);

			if (display_ok)
			{
				ui::show_reading(r.co2_ppm, r.temp_x100, r.hum_x100);
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
