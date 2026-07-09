#pragma once
#include <stdint.h>

/*
 * Exponential moving average for noisy integer sensor readings.
 *
 * Each sample pulls the average 1/2^Shift of the way toward the new reading, so a
 * bigger Shift is a calmer, slower filter: 1 keeps 1/2 of the average, 2 keeps 3/4,
 * 5 keeps 31/32.
 *
 * Rise and fall may use different shifts. A battery gauge should follow a falling cell
 * readily but a rising one reluctantly -- reading low merely looks pessimistic, reading
 * high strands the user. That asymmetry has a price, and it is deliberate: against
 * symmetric noise the filter settles a little BELOW the true mean.
 *
 * The state carries `Frac` fractional bits so the filter always reaches its input
 * exactly. Accumulating in whole units instead (value += (raw - value) >> Shift) stalls
 * as soon as the gap drops below 2^Shift, leaving the output stuck one short forever.
 *
 * Statics are zero-initialized and so start unseeded: the first update() adopts the
 * reading rather than dragging it up from zero. That is a flag, not a sentinel value,
 * because readings may legitimately be negative (sub-zero temperatures).
 */
template <uint8_t RiseShift, uint8_t FallShift = RiseShift>
struct Ema
{
	static_assert(RiseShift >= 1 && RiseShift <= 8, "RiseShift outside the useful range");
	static_assert(FallShift >= 1 && FallShift <= 8, "FallShift outside the useful range");

	/* Enough fractional bits for the slower of the two directions. */
	static constexpr uint8_t Frac = (RiseShift > FallShift) ? RiseShift : FallShift;
	static constexpr int32_t One = 1 << Frac;

	int32_t state; /* the average, in units of 1/One */
	bool seeded;

	/* Arithmetic (floor) shift, so this rounds the same way for negative readings. */
	int32_t value() const { return state >> Frac; }

	int32_t update(int32_t raw)
	{
		if (!seeded)
		{
			state = raw * One;
			seeded = true;
			return raw;
		}

		const int32_t gap = raw - value();
		const uint8_t shift = (gap > 0) ? RiseShift : FallShift;

		/* Multiply rather than shift: gap is signed, and a negative left shift is
		 * undefined before C++20. (One >> shift) is 2^(Frac - shift), never 0, which
		 * is what keeps a one-unit gap from stalling. */
		state += gap * (One >> shift);
		return value();
	}
};
