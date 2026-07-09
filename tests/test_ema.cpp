#include "check.hpp"
#include "util/ema.hpp"

namespace
{
	/* Samples until the filter sits exactly on a constant input, or -1 if it never
	 * does. A filter that accumulates in whole units stalls one short forever. */
	template <typename E>
	int settle(int32_t seed, int32_t raw, int max_iter = 100000)
	{
		E e{};
		e.update(seed);
		for (int i = 1; i <= max_iter; i++)
		{
			if (e.update(raw) == raw && e.value() == raw)
			{
				return i;
			}
		}
		return -1;
	}

	/* How far one sample moves the filter from `seed` toward `raw`. */
	template <typename E>
	int32_t first_step(int32_t seed, int32_t raw)
	{
		E e{};
		e.update(seed);
		const int32_t moved = e.update(raw) - seed;
		return (moved < 0) ? -moved : moved;
	}

	/* Mean output when fed a constant plus symmetric +-1 noise, relative to truth. */
	template <typename E>
	double noise_bias(int32_t truth)
	{
		E e{};
		e.update(truth);
		uint32_t rng = 12345;
		long long sum = 0;
		constexpr int N = 50000;
		for (int i = 0; i < N; i++)
		{
			rng = rng * 1664525u + 1013904223u;
			const int32_t noise = (int32_t)((rng >> 16) % 3) - 1; /* -1, 0, +1 */
			sum += e.update(truth + noise);
		}
		return (double)sum / N - truth;
	}
} // namespace

void test_ema()
{
	/* The first sample is adopted, not averaged in from zero. */
	{
		Ema<5, 3> e{};
		CHECK_EQ(e.update(3800), 3800);
		Ema<2> t{};
		CHECK_EQ(t.update(-1234), -1234); /* seeds fine below zero */
	}

	/* Never stalls: reaches a constant input exactly, from either side, including
	 * negative values and across zero. The old (value*3 + raw)/4 form did not. */
	CHECK(settle<Ema<2>>(99, 100) > 0);
	CHECK(settle<Ema<2>>(101, 100) > 0);
	CHECK(settle<Ema<5, 3>>(99, 100) > 0);
	CHECK(settle<Ema<5, 3>>(101, 100) > 0);
	CHECK(settle<Ema<2>>(2500, -4500) > 0); /* room temp -> -45 C */
	CHECK(settle<Ema<2>>(-2001, -2000) > 0);
	CHECK(settle<Ema<2>>(-1999, -2000) > 0);
	CHECK(settle<Ema<5, 3>>(500, -500) > 0);
	CHECK(settle<Ema<5, 3>>(-500, 500) > 0);

	/* A bigger shift is a slower filter. */
	CHECK(settle<Ema<4>>(0, 1000) > settle<Ema<2>>(0, 1000));

	/* Equal shifts move the same distance per sample in both directions -- to within
	 * the one unit that value()'s floor rounding can shave off a downward step. */
	{
		const int32_t up = first_step<Ema<3>>(3700, 3800);
		const int32_t down = first_step<Ema<3>>(3800, 3700);
		CHECK(up > 0 && down > 0);
		CHECK(down - up <= 1 && up - down <= 1);
	}

	/* The battery gauge falls ~4x more readily than it rises (Ema<5,3>). */
	{
		const int32_t up = first_step<Ema<5, 3>>(3700, 3800);
		const int32_t down = first_step<Ema<5, 3>>(3800, 3700);
		CHECK(down >= 3 * up);
		CHECK(settle<Ema<5, 3>>(3800, 3700) < settle<Ema<5, 3>>(3700, 3800));
	}

	/* Never overshoots a constant input, and approaches it monotonically. */
	{
		Ema<5, 3> e{};
		int32_t prev = e.update(3700);
		for (int i = 0; i < 500; i++)
		{
			const int32_t now = e.update(3800);
			CHECK(now >= prev);
			CHECK(now <= 3800);
			prev = now;
		}
	}

	/* The asymmetry biases the reading low against symmetric noise. That is the price,
	 * and it is paid in the safe direction; a symmetric filter must not pay it. */
	{
		const double sym = noise_bias<Ema<2>>(3780);
		const double asym = noise_bias<Ema<5, 3>>(3780);
		CHECK(sym > -0.2 && sym < 0.2); /* unbiased */
		CHECK(asym < -0.1);             /* pessimistic */
		CHECK(asym > -3.0);             /* but only slightly */
	}
}
