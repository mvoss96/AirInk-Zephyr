#pragma once
#include <stdint.h>

/*
 * Battery monitor. Two SAADC channels from the devicetree `zephyr,user` io-channels:
 *   [0] ext = external divider on P0.31 (300k BAT+ / 100k GND -> V_bat/4)
 *   [1] int = SoC internal VDDH/5 path (no external parts)
 *
 * The external divider is the gauge. The internal path is read only to compare the
 * two: on USB, VDDH sits well above the cell, which is how charging is detected. Its
 * own voltage is of no interest to callers and does not leave the module.
 */

/* Voltage/percent are EMA-smoothed inside sample() (the SAADC jitters a few mV, which
 * bounces the % on the steep Li-Ion curve); `charging` is instantaneous. */
struct BatteryReading
{
	int32_t bat_mv;  /* battery voltage via P0.31 divider (BAT+), smoothed */
	uint8_t bat_pct; /* 0..100 */
	bool charging;   /* USB present: VDDH sits well above BAT+ (instantaneous) */
};

namespace battery
{
	/* Ready-check and configure both ADC channels. 0 on success, <0 if the
	 * ADC is not ready. */
	int init();

	/* Read both channels and fill *out. 0 on success, <0 on a read error. */
	int sample(BatteryReading *out);
}
