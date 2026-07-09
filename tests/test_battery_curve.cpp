#include <doctest.h>

#include "sensors/battery_curve.hpp"

using battery::kCurve;
using battery::kCurveLen;

namespace
{
	/* mv_to_percent returns uint8_t, which doctest would stringify as a character. */
	int pct(int32_t mv) { return (int)battery::mv_to_percent(mv); }
} // namespace

TEST_CASE("battery curve: clamps at both ends")
{
	/* A flat cell and a charger overshoot must not wrap around. */
	CHECK(pct(0) == 0);
	CHECK(pct(3299) == 0);
	CHECK(pct(3300) == 0);
	CHECK(pct(4200) == 100);
	CHECK(pct(5000) == 100);
}

TEST_CASE("battery curve: exact on every knot")
{
	for (size_t i = 0; i < kCurveLen; i++)
	{
		CAPTURE(kCurve[i].mv);
		REQUIRE(pct(kCurve[i].mv) == (int)kCurve[i].pct);
	}
}

TEST_CASE("battery curve: never decreasing, always a valid percentage")
{
	int last = -1;
	for (int32_t mv = 3200; mv <= 4300; mv++)
	{
		CAPTURE(mv);
		const int p = pct(mv);
		REQUIRE(p >= last);
		REQUIRE(p >= 0);
		REQUIRE(p <= 100);
		last = p;
	}
}

TEST_CASE("battery curve: interpolates linearly between knots")
{
	CHECK(pct(3450) == 2); /* midway 3300..3600 -> 2.5 %, truncated */
	CHECK(pct(3640) == 7); /* midway 3600..3680 -> 7.5 %, truncated */
	CHECK(pct(3780) == 35);
}

TEST_CASE("battery curve: the slope varies 30-fold across the range")
{
	/* 60 mV per percentage point near empty, but only 2 mV per point at 30-40 %. That
	 * steepness is why the SAADC noise had to be fought at the source. */
	CHECK(pct(3360) - pct(3300) == 1);
	CHECK(pct(3772) - pct(3770) == 1);
}

TEST_CASE("battery curve: the low-battery thresholds sit where dither cannot reach")
{
	CHECK(pct(3600) == 5); /* LOW_BATTERY_ENTER_PCT */
	CHECK(pct(3648) == 8); /* LOW_BATTERY_EXIT_PCT */
}
