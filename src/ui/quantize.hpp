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
	/* To 0.1 C. Ties round away from zero: -0.25 C -> -0.3, +0.25 C -> +0.3. */
	inline int32_t quantize_temp_x100(int32_t temp_x100)
	{
		return ((temp_x100 + (temp_x100 >= 0 ? 5 : -5)) / 10) * 10;
	}

	/* To whole percent. Relative humidity is never negative. */
	inline uint16_t quantize_hum_x100(uint16_t hum_x100)
	{
		return (uint16_t)(((hum_x100 + 50) / 100) * 100);
	}
} // namespace ui
