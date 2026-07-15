#pragma once
#include <stdint.h>

/** @file
 * Battery monitor: state of charge, USB charge detection, and the low-battery latch.
 *
 * Two SAADC channels ([0] external divider on P0.31 = V_bat/4, [1] internal VDDH/5). The divider
 * is the gauge; the internal path exists only for charge detection -- on USB, VDDH sits well above
 * the cell. Cell voltage, smoothing and conversion failures stay inside the module: callers ask
 * what the battery is doing and get an answer.
 */
namespace battery
{
	struct State
	{
		uint8_t pct;   // 0..100, EMA-smoothed
		bool charging; // USB present (instantaneous, from the raw voltages)
		bool low;      // latched with hysteresis (enter 5 %, exit 8 %); charging releases it
	};

	/** Configure both ADC channels. @retval -ENODEV SAADC not ready */
	int init();

	/** Call back the instant USB power appears or disappears (POWER interrupt: wake somebody,
	 * touch nothing else). Without it the charging bolt lags by up to one 30 s cycle.
	 * @retval negative: no VBUS events; the bolt then updates on the next cycle as it used to */
	int watch_supply(void (*on_change)());

	/** Read both channels and derive the state. The first call seeds the smoothing filter; a
	 * failed conversion logs and returns the previous state unchanged, latch included. */
	State read();
}
