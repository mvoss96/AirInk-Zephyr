#pragma once
#include <stdint.h>
#include <stdio.h>

/*
 * Rounding of readings to what the panel actually shows. set_sensor() dedups on these, so a change
 * too small to display costs no e-paper refresh (~0.16 mAs per stable tick instead of ~3 mAs).
 * Sub-zero is the trap throughout: integer division truncates toward zero.
 */
namespace ui
{
	/** Round to the 0.1 C the panel shows; ties away from zero. */
	inline int32_t quantize_temp_x100(int32_t temp_x100)
	{
		return ((temp_x100 + (temp_x100 >= 0 ? 5 : -5)) / 10) * 10;
	}

	/** Celsius in, whole Fahrenheit out (x100). Whole degrees on purpose: 1 F is 5.6x coarser than
	 * the displayed 0.1 C, so the panel refreshes LESS often in F -- tenths of a F would be finer
	 * and cost refreshes instead. */
	inline int32_t quantize_temp_f_x100(int32_t temp_x100)
	{
		const int32_t f_x100 = temp_x100 * 9 / 5 + 3200;
		return ((f_x100 + (f_x100 >= 0 ? 50 : -50)) / 100) * 100;
	}

	/** Signal bars for a Thread RSSI: 0 (attached, barely) to 4. The hysteresis is not decoration:
	 * this feeds the status bar's dedup, and a link parked on a boundary -- a badly placed device,
	 * exactly who needs the bars -- would otherwise buy a ~3 mAs refresh every 30 s.
	 * @param prev_bars the level being held (0..4), or -1 for none */
	inline int quantize_signal_bars(int rssi_dbm, int prev_bars)
	{
		constexpr int SIGNAL_HYST_DB = 3;
		// RSSI needed to REACH each level; [0] unreachable by design.
		constexpr int reach[5] = {-128, -95, -85, -75, -65};

		for (int b = 4; b >= 1; b--)
		{
			// At this level or above? Hold it a few dB longer before giving it up.
			const int t = (b <= prev_bars) ? reach[b] - SIGNAL_HYST_DB : reach[b];
			if (rssi_dbm >= t)
			{
				return b;
			}
		}
		return 0;
	}

	/** Write a hundredths value as a person reads it: "-0.4", "23.5", "77". Exists because the
	 * obvious `"%d.%d", v/100, abs(v%100)/10` loses the minus for -1.00 < v < 0 (truncation toward
	 * zero) -- it printed "0.5" for half a degree of frost, in three places.
	 * @param decimals 1 -> "23.5"; 0 -> "77" (round before calling) */
	inline void format_x100(char *buf, size_t n, int32_t v_x100, int decimals)
	{
		const char *sign = (v_x100 < 0) ? "-" : "";
		const int32_t a = (v_x100 < 0) ? -v_x100 : v_x100;

		if (decimals == 1)
		{
			snprintf(buf, n, "%s%d.%d", sign, (int)(a / 100), (int)((a % 100) / 10));
		}
		else
		{
			snprintf(buf, n, "%s%d", sign, (int)(a / 100));
		}
	}

	/** Round to the whole percent the panel shows (never negative). */
	inline uint16_t quantize_hum_x100(uint16_t hum_x100)
	{
		return (uint16_t)(((hum_x100 + 50) / 100) * 100);
	}
} // namespace ui
