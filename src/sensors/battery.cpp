#include "battery.hpp"
#include "battery_curve.hpp"
#include "../util/ema.hpp"

#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/adc.h>

namespace
{
	// [0] = external divider (P0.31, x4), [1] = internal VDDH/5 (x5).
	const struct adc_dt_spec ch_ext = ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 0);
	const struct adc_dt_spec ch_int = ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 1);

	/* USB is present (charging) when the supply rail (VDDH) sits well above the
	 * battery terminal (BAT+). On battery only, the two are ~equal; on USB, VDDH is
	 * the ~4.8 V USB rail, hundreds of mV above the cell. */
	constexpr int32_t CHARGE_DETECT_MV = 300;

	/** Read one channel as a cell voltage: pin voltage x the divider ratio.
	 * Scale the raw counts BEFORE converting. adc_raw_to_millivolts_dt() truncates to
	 * whole millivolts at the pin, so converting first and multiplying afterwards
	 * multiplies that rounding error too: the cell voltage would land on multiples of
	 * `scale` mV (4 mV here -- two percentage points on the steep part of the Li-Ion
	 * curve). The conversion is linear, so pre-scaling is exact and costs nothing.
	 *
	 * @param      ch     the configured ADC channel to sample
	 * @param      scale  divider ratio: 4 for the external divider, 5 for VDDH/5
	 * @param[out] mv_out receives the cell voltage in millivolts
	 * @retval 0      on success
	 * @retval -errno the conversion failed; *mv_out is left untouched
	 */
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

		int32_t mv = (int32_t)raw * scale;
		err = adc_raw_to_millivolts_dt(&ch, &mv);
		if (err < 0)
		{
			return err;
		}

		*mv_out = mv;
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

	// Charging is a step event (USB plugged in): detect it on the raw, instantaneous
	// voltages so the bolt appears at once instead of after the EMA catches up. This
	// is the only use of the internal channel; its voltage goes no further.
	out->charging = (int_raw - ext_raw) > CHARGE_DETECT_MV;

	out->bat_mv = ext_mv_ema.update(ext_raw);
	out->bat_pct = mv_to_percent(out->bat_mv);
	return 0;
}
