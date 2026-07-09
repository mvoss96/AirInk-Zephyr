#pragma once
#include <stdint.h>
#include <stddef.h>

/* Open-circuit voltage -> state of charge for a typical single-cell Li-Ion, piecewise
 * linearized.
 */
namespace battery
{
	struct CurvePoint
	{
		int32_t mv;
		uint8_t pct;
	};

	inline constexpr CurvePoint kCurve[] = {
		{3300, 0},
		{3600, 5},
		{3680, 10},
		{3740, 20},
		{3770, 30},
		{3790, 40},
		{3820, 50},
		{3870, 60},
		{3920, 70},
		{4000, 80},
		{4110, 90},
		{4200, 100},
	};
	inline constexpr size_t kCurveLen = sizeof(kCurve) / sizeof(kCurve[0]);

	/** Map a cell voltage to a state of charge by linear interpolation.
	 *
	 * @param mv open-circuit cell voltage in millivolts
	 * @return 0..100 %, clamped at both ends of the curve
	 */
	inline uint8_t mv_to_percent(int32_t mv)
	{
		if (mv <= kCurve[0].mv)
		{
			return 0;
		}
		if (mv >= kCurve[kCurveLen - 1].mv)
		{
			return 100;
		}

		for (size_t i = 1; i < kCurveLen; ++i)
		{
			if (mv <= kCurve[i].mv)
			{
				const CurvePoint lo = kCurve[i - 1];
				const CurvePoint hi = kCurve[i];
				// Interpolate in int: the mV deltas overflow a uint8_t.
				const int pct = lo.pct + (int)((mv - lo.mv) * (hi.pct - lo.pct) / (hi.mv - lo.mv));
				return (uint8_t)pct;
			}
		}
		return 100;
	}
} // namespace battery
