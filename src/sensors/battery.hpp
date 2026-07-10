#pragma once
#include <stdint.h>

/** @file
 * Battery monitor: state of charge, USB charge detection, and the low-battery latch.
 *
 * Two SAADC channels from the devicetree `zephyr,user` io-channels:
 *   [0] ext = external divider on P0.31 (300k BAT+ / 100k GND -> V_bat/4)
 *   [1] int = SoC internal VDDH/5 path (no external parts)
 *
 * The external divider is the gauge. The internal path is read only to compare the
 * two: on USB, VDDH sits well above the cell, which is how charging is detected. Its
 * own voltage is of no interest to callers and does not leave the module.
 *
 * Neither does the cell voltage, the smoothing, or the fact that a conversion can fail.
 * Callers ask what the battery is doing and get an answer.
 */
namespace battery
{
	// The latch engages at or below the first, releases at or above the second. Charging
	// always releases it. The gap absorbs the +-1 pp jitter the smoothed percent still
	// has near a boundary -- a flapping latch would be a view change on the panel, i.e.
	// a full-refresh black flash every 30 s.
	constexpr uint8_t LOW_ENTER_PCT = 5;
	constexpr uint8_t LOW_EXIT_PCT = 8;

	/** What the battery is doing. */
	struct State
	{
		uint8_t pct;   // 0..100, EMA-smoothed
		bool charging; // USB present: VDDH sits well above BAT+ (instantaneous)
		bool low;      // latched; see LOW_ENTER_PCT / LOW_EXIT_PCT
	};

	/** Ready-check and configure both ADC channels.
	 *
	 * @retval 0       both channels are set up
	 * @retval -ENODEV the SAADC is not ready
	 * @retval -errno  a channel could not be configured
	 */
	int init();

	/** Read both channels and derive the state.
	 *
	 * The first call seeds the smoothing filter, so it already returns a settled value
	 * rather than ramping up from zero. A failed conversion is logged and returns the
	 * previous state unchanged -- including the latch, which must not flap on an I2C
	 * hiccup any more than on noise.
	 *
	 * @return the current state
	 */
	State read();
}
