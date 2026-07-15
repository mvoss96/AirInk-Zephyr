#pragma once
#include <stdint.h>

namespace ui
{
	/** The unit the panel shows temperature in. A display preference only: the sensor and the
	 * Matter cluster stay Celsius. prefs persists it, display_ui paints it -- this lives in its
	 * own header so neither drags the other's full API in for one enum. */
	enum class TempUnit : uint8_t
	{
		Celsius,
		Fahrenheit
	};
} // namespace ui
