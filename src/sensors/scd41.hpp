#pragma once
#include <stdint.h>

/** @file
 * SCD41 CO2/temperature/humidity sensor (I2C0, single-shot mode).
 *
 * Wraps the Zephyr SCD4X driver -- and raw I2C where the driver has no path, like the cheap
 * T+RH-only single-shot. Device from devicetree node `scd41`.
 */

struct Scd41Reading
{
	uint16_t co2_ppm;  // CO2 in ppm
	int32_t temp_x100; // temperature in C * 100
	uint16_t hum_x100; // relative humidity in % * 100
};

namespace scd41
{
	/** What the sensor is told about where it is standing. All three are things only a human
	 * knows: the offset compensates self-heating (factory 4.0 C, right value depends on the
	 * case); altitude corrects for thinner air (matters above ~200 m); self-calibration trims
	 * against weeks of fresh-air minima -- right in an aired room, wrong in one that never is.
	 * Defaults are the sensor's own, except self-calibration, which has always shipped off. */
	struct Trim
	{
		int temp_offset_x10 = 40; // tenths of a degree C, 0..200
		int altitude_m = 0;		  // metres above sea level, 0..3000
		bool auto_calib = false;  // ASC
	};

	/** Check the device is ready -- deliberately nothing more. The trim is the user's and comes
	 * from prefs::apply_all() a moment later; factory defaults here would be three I2C writes the
	 * next three undo. The bench harness, which has no prefs, measures the sensor's EEPROM state,
	 * which is exactly what a bench should.
	 * @retval -ENODEV node `scd41` not ready */
	int init();

	/** Tell the sensor its trim. Volatile on purpose: nothing touches the SCD41's EEPROM (a few
	 * thousand writes of life); prefs re-pushes on every boot at the cost of three I2C transfers.
	 * @retval -EIO at least one value was refused (logged; the others still took) */
	int set_trim(const Trim &t);

	/** Full single-shot: CO2 + T + RH. Blocks ~5 s (photoacoustic pulse, ~74 mAs -- ~90 % of the
	 * device's active energy; see docs/power-analysis.md). */
	int sample(Scd41Reading *out);

	/** T + RH only (command 0x2196): ~50 ms, ~1000x less energy than a full read. What the 30 s
	 * tick uses between the five-minute CO2 reads. co2_ppm is set to 0. */
	int sample_rht(Scd41Reading *out);

	/** Enter periodic measurement, the mode a forced recalibration requires (a few minutes of
	 * measuring the target air; single-shot does not count). sample()/sample_rht() must NOT be
	 * called until calibrate_finish() or calibrate_abort(). */
	int calibrate_begin();

	/** Force-recalibrate against a known concentration, then back to single-shot, powered down.
	 * @param[out] correction_ppm how far it moved; may be negative
	 * @retval -EIO refused (the sensor answers 0xFFFF for an implausible reading) or I2C failure;
	 *              calibration unchanged */
	int calibrate_finish(uint16_t target_ppm, int16_t *correction_ppm);

	/** Leave periodic measurement without recalibrating. */
	int calibrate_abort();
} // namespace scd41
