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

	/** Put the sensor into periodic measurement, the mode a recalibration needs.
	 *
	 * A forced recalibration is only valid if the sensor has been measuring the target
	 * air in its normal operating mode for a few minutes. Single-shot does not count,
	 * so the flow switches modes, waits, and switches back.
	 *
	 * While this is in effect, sample() and sample_rht() must NOT be called: they issue
	 * single-shot commands the sensor will not honour mid-measurement.
	 *
	 * @retval 0     the sensor is measuring every 5 s
	 * @retval -EIO  an I2C transfer failed
	 */
	int calibrate_begin();

	/** Recalibrate against a known concentration, then return to single-shot.
	 *
	 * Stops periodic measurement, waits out the sensor's settling time, and runs the
	 * forced recalibration. The driver leaves the sensor powered down afterwards, i.e.
	 * back in our normal regime -- no reboot needed.
	 *
	 * @param      target_ppm     what the surrounding air really is, e.g. 420 outdoors
	 * @param[out] correction_ppm how far the calibration moved; may be negative
	 * @retval 0     recalibrated
	 * @retval -EIO  the sensor refused (it answers 0xFFFF when the reading was
	 *               implausible), or an I2C transfer failed; the calibration is unchanged
	 */
	int calibrate_finish(uint16_t target_ppm, int16_t *correction_ppm);

	/** Leave periodic measurement without recalibrating anything.
	 *
	 * @retval 0     back in single-shot, sensor powered down
	 * @retval -EIO  an I2C transfer failed
	 */
	int calibrate_abort();
} // namespace scd41
