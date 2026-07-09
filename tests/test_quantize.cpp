#include "check.hpp"
#include "ui/quantize.hpp"

using ui::quantize_hum_x100;
using ui::quantize_temp_x100;

void test_quantize()
{
	/* Temperature rounds to the 0.1 C the panel shows. */
	CHECK_EQ(quantize_temp_x100(0), 0);
	CHECK_EQ(quantize_temp_x100(2400), 2400);
	CHECK_EQ(quantize_temp_x100(2404), 2400);
	CHECK_EQ(quantize_temp_x100(2405), 2410); /* tie rounds up */
	CHECK_EQ(quantize_temp_x100(2409), 2410);

	/* Below zero. This is the path that has no other way to be checked, and it would
	 * fail silently in winter: truncating division rounds toward zero, so the tie has
	 * to be nudged away from it by hand. */
	CHECK_EQ(quantize_temp_x100(-2400), -2400);
	CHECK_EQ(quantize_temp_x100(-2404), -2400);
	CHECK_EQ(quantize_temp_x100(-2405), -2410); /* tie rounds away from zero */
	CHECK_EQ(quantize_temp_x100(-2409), -2410);
	CHECK_EQ(quantize_temp_x100(-4), 0);
	CHECK_EQ(quantize_temp_x100(-5), -10);
	CHECK_EQ(quantize_temp_x100(-4500), -4500); /* SCD41's lower limit */

	/* Symmetric about zero: |q(-x)| == |q(x)|. An asymmetric rounding would show a
	 * different last digit either side of freezing. */
	for (int32_t t = 0; t <= 5000; t++)
	{
		CHECK_EQ(quantize_temp_x100(-t), -quantize_temp_x100(t));
	}

	/* Idempotent: quantizing an already-quantized value changes nothing, which is what
	 * makes the dedup in set_sensor stable. */
	for (int32_t t = -4500; t <= 5000; t += 7)
	{
		const int32_t q = quantize_temp_x100(t);
		CHECK_EQ(quantize_temp_x100(q), q);
		CHECK_EQ(q % 10, 0);
	}

	/* Never off by more than half a step. */
	for (int32_t t = -4500; t <= 5000; t++)
	{
		const int32_t d = quantize_temp_x100(t) - t;
		CHECK(d >= -5 && d <= 5);
	}

	/* Humidity rounds to whole percent; it is never negative. */
	CHECK_EQ(quantize_hum_x100(0), 0);
	CHECK_EQ(quantize_hum_x100(4149), 4100);
	CHECK_EQ(quantize_hum_x100(4150), 4200); /* tie rounds up */
	CHECK_EQ(quantize_hum_x100(4199), 4200);
	CHECK_EQ(quantize_hum_x100(10000), 10000);

	for (uint16_t h = 0; h <= 10000; h++)
	{
		const uint16_t q = quantize_hum_x100(h);
		CHECK_EQ(q % 100, 0);
		CHECK(q <= 10000);
		CHECK_EQ(quantize_hum_x100(q), q); /* idempotent */
	}
}
