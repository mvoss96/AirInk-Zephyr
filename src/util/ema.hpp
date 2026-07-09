#pragma once
#include <stdint.h>

/*
 * Exponential moving average for noisy integer sensor readings:
 *
 *     value = (3 * value + raw) / 4
 *
 * i.e. a 3/4 weight on the running average. Zero-initialized instances (statics)
 * start unseeded, so the first update() adopts the raw reading directly instead of
 * dragging it up from zero over several samples.
 *
 * `seeded` is an explicit flag rather than a sentinel value because readings may
 * legitimately be negative (e.g. sub-zero temperatures).
 */
struct Ema
{
	int32_t value;
	bool seeded;

	int32_t update(int32_t raw)
	{
		value = seeded ? (value * 3 + raw) / 4 : raw;
		seeded = true;
		return value;
	}
};
