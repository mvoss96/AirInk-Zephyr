#include "check.hpp"
#include "sensors/battery_curve.hpp"

using battery::kCurve;
using battery::kCurveLen;
using battery::mv_to_percent;

void test_battery_curve()
{
	/* Clamped at both ends -- a flat cell and a charger overshoot must not wrap. */
	CHECK_EQ(mv_to_percent(0), 0);
	CHECK_EQ(mv_to_percent(3300), 0);
	CHECK_EQ(mv_to_percent(3299), 0);
	CHECK_EQ(mv_to_percent(4200), 100);
	CHECK_EQ(mv_to_percent(5000), 100);

	/* Exact on every knot of the curve. */
	for (size_t i = 0; i < kCurveLen; i++)
	{
		CHECK_EQ(mv_to_percent(kCurve[i].mv), kCurve[i].pct);
	}

	/* Never decreasing, and always a valid percentage. */
	{
		int last = -1;
		for (int32_t mv = 3200; mv <= 4300; mv++)
		{
			const int pct = mv_to_percent(mv);
			CHECK(pct >= last);
			CHECK(pct >= 0 && pct <= 100);
			last = pct;
		}
	}

	/* Interpolates linearly between knots (truncating, so floor). */
	CHECK_EQ(mv_to_percent(3450), 2); /* midway 3300..3600 -> 2.5 % */
	CHECK_EQ(mv_to_percent(3640), 7); /* midway 3600..3680 -> 7.5 % */
	CHECK_EQ(mv_to_percent(3780), 35);

	/* The slope the rest of the firmware reasons about: 60 mV per point near empty,
	 * but only 2 mV per point at 30-40 %. This is why the SAADC noise mattered. */
	CHECK_EQ(mv_to_percent(3360) - mv_to_percent(3300), 1);
	CHECK_EQ(mv_to_percent(3772) - mv_to_percent(3770), 1);

	/* The low-battery thresholds sit in the flat region, where dither cannot reach. */
	CHECK_EQ(mv_to_percent(3600), 5); /* LOW_BATTERY_ENTER_PCT */
	CHECK_EQ(mv_to_percent(3648), 8); /* LOW_BATTERY_EXIT_PCT */
}
