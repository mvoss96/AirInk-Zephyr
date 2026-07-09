#pragma once
#include <stdint.h>

/*
 * Rounding of sensor readings to the resolution the panel actually shows. Pulled out of
 * display_ui.cpp so it can be exercised on the host (tests/): the negative-temperature
 * path has no other way to be checked, and it would fail silently below 0 C.
 *
 * set_sensor() dedups on these quantized values, so a change too small to be displayed
 * costs no e-paper refresh.
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
