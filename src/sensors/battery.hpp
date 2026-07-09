#pragma once
#include <stdint.h>

/*
 * Battery monitor. Two SAADC channels are measured every sample so the two
 * sensing paths can be compared (one becomes the primary later):
 *   ext = external divider on P0.31 (300k BAT+ / 100k GND -> V_bat/4)
 *   int = SoC internal VDDH/5 path (no external parts)
 * Channels come from the devicetree `zephyr,user` io-channels [0]=ext, [1]=int.
 */

/* Voltages/percentages are EMA-smoothed inside sample() (the SAADC jitters a few
 * mV, which bounces the % on the steep Li-Ion curve); `charging` is instantaneous. */
struct BatteryReading
{
	int32_t ext_mv; /* battery voltage via P0.31 divider (BAT+), smoothed */
	int ext_pct;
	int32_t int_mv; /* supply voltage via internal VDDH/5, smoothed */
	int int_pct;
	bool charging; /* USB present: VDDH sits well above BAT+ (instantaneous) */
};

namespace battery
{
	/* Ready-check and configure both ADC channels. 0 on success, <0 if the
	 * ADC is not ready. */
	int init();

	/* Read both channels and fill *out. 0 on success, <0 on a read error. */
	int sample(BatteryReading *out);
}
