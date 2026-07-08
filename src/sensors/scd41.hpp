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

	/* Take a full single-shot measurement (CO2 + T + RH, blocks ~5 s) and fill
	 * *out. Returns 0 on success, <0 on a fetch error. */
	int sample(Scd41Reading *out);

	/* T+RH-only single-shot (SCD4x cmd 0x2196, ~50 ms, ~1000x less energy than a
	 * full CO2 read -- see docs/power-analysis.md). Fills temp_x100/hum_x100;
	 * co2_ppm is set to 0. Returns 0 on success, <0 on an I2C error. */
	int sample_rht(Scd41Reading *out);
}
