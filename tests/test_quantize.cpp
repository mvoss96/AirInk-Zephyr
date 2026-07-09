#include <doctest.h>

#include "ui/quantize.hpp"

using ui::quantize_hum_x100;
using ui::quantize_temp_x100;

TEST_CASE("quantize temp: rounds to the 0.1 C the panel shows")
{
	CHECK(quantize_temp_x100(0) == 0);
	CHECK(quantize_temp_x100(2400) == 2400);
	CHECK(quantize_temp_x100(2404) == 2400);
	CHECK(quantize_temp_x100(2405) == 2410); /* tie rounds up */
	CHECK(quantize_temp_x100(2409) == 2410);
}

TEST_CASE("quantize temp: below zero, ties round away from zero")
{
	/* The path with no other way to be checked, and it would fail silently in winter:
	 * truncating division rounds toward zero, so the tie has to be nudged by hand. */
	CHECK(quantize_temp_x100(-2400) == -2400);
	CHECK(quantize_temp_x100(-2404) == -2400);
	CHECK(quantize_temp_x100(-2405) == -2410);
	CHECK(quantize_temp_x100(-2409) == -2410);
	CHECK(quantize_temp_x100(-4) == 0);
	CHECK(quantize_temp_x100(-5) == -10);
	CHECK(quantize_temp_x100(-4500) == -4500); /* the SCD41's lower limit */
}

TEST_CASE("quantize temp: symmetric about zero")
{
	/* An asymmetric rounding would show a different last digit either side of freezing. */
	for (int32_t t = 0; t <= 5000; t++)
	{
		CAPTURE(t);
		REQUIRE(quantize_temp_x100(-t) == -quantize_temp_x100(t));
	}
}

TEST_CASE("quantize temp: idempotent, and always a multiple of 0.1 C")
{
	/* Idempotence is what makes set_sensor's dedup stable. */
	for (int32_t t = -4500; t <= 5000; t++)
	{
		CAPTURE(t);
		const int32_t q = quantize_temp_x100(t);
		REQUIRE(q % 10 == 0);
		REQUIRE(quantize_temp_x100(q) == q);
	}
}

TEST_CASE("quantize temp: never off by more than half a step")
{
	for (int32_t t = -4500; t <= 5000; t++)
	{
		CAPTURE(t);
		const int32_t d = quantize_temp_x100(t) - t;
		REQUIRE(d >= -5);
		REQUIRE(d <= 5);
	}
}

TEST_CASE("quantize hum: rounds to whole percent")
{
	CHECK(quantize_hum_x100(0) == 0);
	CHECK(quantize_hum_x100(4149) == 4100);
	CHECK(quantize_hum_x100(4150) == 4200); /* tie rounds up */
	CHECK(quantize_hum_x100(4199) == 4200);
	CHECK(quantize_hum_x100(10000) == 10000);
}

TEST_CASE("quantize hum: idempotent and bounded over the whole range")
{
	for (uint16_t h = 0; h <= 10000; h++)
	{
		CAPTURE(h);
		const uint16_t q = quantize_hum_x100(h);
		REQUIRE(q % 100 == 0);
		REQUIRE(q <= 10000);
		REQUIRE(quantize_hum_x100(q) == q);
	}
}
