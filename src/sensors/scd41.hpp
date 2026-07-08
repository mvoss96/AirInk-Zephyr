#pragma once
#include <stdint.h>

/*
 * SCD41 CO2/temperature/humidity sensor (I2C0, single-shot mode). Wraps the
 * Zephyr SCD4X driver so main.cpp deals in plain readings, not sensor_value
 * plumbing. Device comes from the devicetree node `scd41`.
 */

struct Scd41Reading
{
	uint16_t co2_ppm;
	int32_t temp_x100; /* °C * 100 */
	uint16_t hum_x100; /* %RH * 100 */
};

namespace scd41
{
	/* Check the device is ready and disable automatic self-calibration.
	 * Returns 0 on success, <0 if the sensor is not present. */
	int init();

	/* Take a single-shot measurement (blocks ~5 s) and fill *out.
	 * Returns 0 on success, <0 on a fetch error. */
	int sample(Scd41Reading *out);
}
