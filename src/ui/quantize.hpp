#pragma once
#include <stdint.h>

/*
 * Rounding of sensor readings to the resolution the panel actually shows.
 *
 * set_sensor() dedups on these quantized values, so a change too small to be displayed
 * costs no e-paper refresh -- that is what keeps a stable T+RH tick at ~0.16 mAs
 * instead of ~3 mAs. The sub-zero path is easy to get wrong: integer division
 * truncates toward zero, so the tie has to be nudged away from it by hand.
 */
namespace ui
{
	/** Round a temperature to the 0.1 C the panel shows.
	 * Ties round away from zero: -0.25 C -> -0.3, +0.25 C -> +0.3.
	 *
	 * @param temp_x100 temperature in hundredths of a degree (may be negative)
	 * @return the same value snapped to a multiple of 10 (i.e. 0.1 C)
	 */
	inline int32_t quantize_temp_x100(int32_t temp_x100)
	{
		return ((temp_x100 + (temp_x100 >= 0 ? 5 : -5)) / 10) * 10;
	}

	/** Convert a Celsius reading to Fahrenheit and round it to the whole degree the panel shows.
	 *
	 * Whole degrees, not tenths, and that is the reason this is not simply quantize_temp_x100 applied
	 * to a converted value: 1 F is 5.6x COARSER than the 0.1 C shown in the other unit, so the
	 * displayed number changes -- and the panel refreshes -- markedly less often. Tenths of a degree
	 * Fahrenheit would be 1.8x FINER than tenths of a Celsius, and would cost refreshes instead.
	 *
	 * The conversion truncates by under 0.01 F, which cannot survive rounding to a whole degree.
	 *
	 * @param temp_x100 temperature in hundredths of a degree CELSIUS (may be negative)
	 * @return the temperature in hundredths of a degree FAHRENHEIT, snapped to a multiple of 100
	 */
	inline int32_t quantize_temp_f_x100(int32_t temp_x100)
	{
		const int32_t f_x100 = temp_x100 * 9 / 5 + 3200;
		return ((f_x100 + (f_x100 >= 0 ? 50 : -50)) / 100) * 100;
	}

	/** Round a humidity to the whole percent the panel shows.
	 *
	 * @param hum_x100 relative humidity in hundredths of a percent (never negative)
	 * @return the same value snapped to a multiple of 100 (i.e. 1 %)
	 */
	inline uint16_t quantize_hum_x100(uint16_t hum_x100)
	{
		return (uint16_t)(((hum_x100 + 50) / 100) * 100);
	}
} // namespace ui
