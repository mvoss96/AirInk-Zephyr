#pragma once
#include <stdint.h>

/** @file
 * SCD41 CO2/temperature/humidity sensor (I2C0, single-shot mode).
 *
 * Wraps the Zephyr SCD4X driver so main.cpp deals in plain readings, not sensor_value
 * plumbing. Device comes from the devicetree node `scd41`.
 */

struct Scd41Reading
{
	uint16_t co2_ppm;  // CO2 in ppm
	int32_t temp_x100; // temperature in C * 100
	uint16_t hum_x100; // relative humidity in % * 100
};

namespace scd41
{
	/** Check the device is ready and disable automatic self-calibration.
	 *
	 * @retval 0       the sensor is present and configured
	 * @retval -ENODEV the devicetree node `scd41` is not ready
	 */
	int init();

	/** Take a full single-shot measurement: CO2 + temperature + humidity.
	 * Blocks for ~5 s while the photoacoustic CO2 measurement runs.
	 *
	 * @param[out] out receives co2_ppm, temp_x100 and hum_x100
	 * @retval 0     on success
	 * @retval -EIO  the driver could not fetch a sample
	 */
	int sample(Scd41Reading *out);

	/** Take a temperature + humidity single-shot, skipping CO2.
	 * SCD4x command 0x2196, ~50 ms and ~1000x less energy than a full CO2 read --
	 * see docs/power-analysis.md. This is what the 30 s tick uses between the
	 * five-minute CO2 reads.
	 *
	 * @param[out] out receives temp_x100 and hum_x100; co2_ppm is set to 0
	 * @retval 0     on success
	 * @retval -EIO  an I2C transfer failed
	 */
	int sample_rht(Scd41Reading *out);
} // namespace scd41
