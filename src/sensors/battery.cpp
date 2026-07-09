#include "battery.hpp"
#include "../util/ema.hpp"

#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/sys/util.h>

namespace
{

	/* [0] = external divider (P0.31, x4), [1] = internal VDDH/5 (x5). */
	const struct adc_dt_spec ch_ext = ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 0);
	const struct adc_dt_spec ch_int = ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 1);

	/* USB is present (charging) when the supply rail (VDDH) sits well above the
	 * battery terminal (BAT+). On battery only, the two are ~equal; on USB, VDDH is
	 * the ~4.8 V USB rail, hundreds of mV above the cell. */
	constexpr int32_t CHARGE_DETECT_MV = 300;

	struct BatteryPoint
	{
		int32_t mv;
		int pct;
	};

	/* Typical single-cell Li-Ion open-circuit curve, piecewise-linearized. */
	constexpr BatteryPoint kCurve[] = {
		{3300, 0},
		{3600, 5},
		{3680, 10},
		{3740, 20},
		{3770, 30},
		{3790, 40},
		{3820, 50},
		{3870, 60},
		{3920, 70},
		{4000, 80},
		{4110, 90},
		{4200, 100},
	};

	/* Read one channel and scale the pin millivolts up to cell voltage. */
	int read_cell_mv(const struct adc_dt_spec &ch, int scale, int32_t *mv_out)
	{
		int16_t raw = 0;
		struct adc_sequence seq = {
			.buffer = &raw,
			.buffer_size = sizeof(raw),
		};

		int err = adc_sequence_init_dt(&ch, &seq);
		if (err == 0)
		{
			err = adc_read(ch.dev, &seq);
		}
		if (err < 0)
		{
			return err;
		}

		int32_t mv = raw;
		err = adc_raw_to_millivolts_dt(&ch, &mv);
		if (err < 0)
		{
			return err;
		}

		*mv_out = mv * scale;
		return 0;
	}

	// Filter the mV, not the derived integer % -- the continuous quantity keeps the
	// filter's sub-percent resolution. SAADC jitter of a few mV bounces the percent by
	// several points on the steep part of the Li-Ion curve.
	//
	// The battery gauge falls ~4x more readily than it rises: a cell that reads low is
	// merely pessimistic (~0.25 pp at the steepest point), one that reads high strands
	// the user. Nothing needs tracking quickly -- charging is detected on the raw
	// reading, and the slow settle costs nothing because the first sample seeds it.
	Ema<5, 3> ext_mv_ema;

	uint8_t mv_to_percent(int32_t mv)
	{
		if (mv <= kCurve[0].mv)
		{
			return 0;
		}
		if (mv >= kCurve[ARRAY_SIZE(kCurve) - 1].mv)
		{
			return 100;
		}
		for (size_t i = 1; i < ARRAY_SIZE(kCurve); ++i)
		{
			if (mv <= kCurve[i].mv)
			{
				const BatteryPoint lo = kCurve[i - 1];
				const BatteryPoint hi = kCurve[i];
				/* Interpolate in int (mv deltas overflow uint8_t); result is 0..100. */
				const int pct = lo.pct + (int)((mv - lo.mv) * (hi.pct - lo.pct) / (hi.mv - lo.mv));
				return (uint8_t)pct;
			}
		}
		return 100;
	}

} // namespace

int battery::init()
{
	if (!adc_is_ready_dt(&ch_ext) || !adc_is_ready_dt(&ch_int))
	{
		return -ENODEV;
	}
	if (int err = adc_channel_setup_dt(&ch_ext); err < 0)
	{
		return err;
	}
	if (int err = adc_channel_setup_dt(&ch_int); err < 0)
	{
		return err;
	}
	return 0;
}

int battery::sample(BatteryReading *out)
{
	int32_t ext_raw, int_raw;
	if (read_cell_mv(ch_ext, 4, &ext_raw) < 0)
	{
		return -EIO;
	}
	if (read_cell_mv(ch_int, 5, &int_raw) < 0)
	{
		return -EIO;
	}

	/* Charging is a step event (USB plugged in): detect it on the raw, instantaneous
	 * voltages so the bolt appears at once instead of after the EMA catches up. This
	 * is the only use of the internal channel; its voltage goes no further. */
	out->charging = (int_raw - ext_raw) > CHARGE_DETECT_MV;

	out->ext_mv = ext_mv_ema.update(ext_raw);
	out->ext_pct = mv_to_percent(out->ext_mv);
	return 0;
}
