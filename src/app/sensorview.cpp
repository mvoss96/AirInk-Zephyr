#include "sensorview.hpp"

#include <stdint.h>
#include <stdlib.h>
#include <zephyr/kernel.h>

#include "sensors/scd41.hpp"
#include "ui/display_ui.hpp"

namespace
{
	/* How many measurement cycles fit between two full CO2 reads. main.cpp ticks every
	 * 30 s, so ten of them is the five-minute CO2 cadence the power budget assumes: a
	 * CO2 single-shot is ~70 mAs, a T+RH read ~0.07 mAs. */
	constexpr uint32_t CO2_EVERY_TICKS = 10;

	uint32_t tick_count;   // cycles since boot, or since the last recalibration
	uint16_t last_co2_ppm; // held on screen between the five-minute CO2 reads
}

void sensorview::measure(bool force_co2)
{
	if (force_co2)
	{
		tick_count = 0;
	}
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

	ui::set_sensor(r.co2_ppm, r.temp_x100, r.hum_x100);
	tick_count++;
}

void sensorview::show()
{
	ui::show_sensor();
}
