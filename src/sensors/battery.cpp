#include "battery.hpp"
#include "battery_curve.hpp"
#include "../util/ema.hpp"

#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/adc.h>

#include <hal/nrf_power.h>
#include <nrfx_power.h>

namespace
{
	// Whoever wants to know the moment the cable moves. Null until watch_supply().
	void (*supply_cb)();

	/** The SoC saw VBUS appear or disappear.
	 *
	 * Runs in the POWER interrupt (shared with the clock driver, which is what already owns
	 * the vector), so it does the one thing an ISR may do here: hand the news on. Reading the
	 * ADC or touching the panel from here would be a fault.
	 */
	void on_usb_evt(nrfx_power_usb_evt_t event)
	{
		// READY means the USB regulator settled; the cable did not move, so nothing to say.
		if (event == NRFX_POWER_USB_EVT_READY || supply_cb == nullptr)
		{
			return;
		}
		supply_cb();
	}

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

	// Survives a failed conversion, so a hiccup neither zeroes the gauge nor flaps the
	// latch. Zero-initialized: at boot the first successful read fills it in.
	battery::State state;

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

int battery::watch_supply(void (*on_change)())
{
	supply_cb = on_change;

	/* nrfx_power_init() writes DCDCEN and DCDCEN_VDDH from its config -- it is not just an
	 * "enable the module" call. Zephyr has already set both (the board's regulator nodes /
	 * CONFIG_BOARD_ENABLE_DCDC), and handing it a zeroed config would switch the DC/DC
	 * converters back off, which is a power regression measured in hundreds of microamps. So
	 * read the state back out of the peripheral and pass it through unchanged: init then
	 * writes exactly what is already there.
	 */
	nrfx_power_config_t cfg{};
	cfg.dcdcen = nrf_power_dcdcen_get(NRF_POWER);
#if NRF_POWER_HAS_DCDCEN_VDDH
	cfg.dcdcenhv = nrf_power_dcdcen_vddh_get(NRF_POWER);
#endif

	// -EALREADY only means somebody got here first, which is fine: the DC/DC settings are then
	// theirs, and they are the same ones.
	const int err = nrfx_power_init(&cfg);
	if (err != 0 && err != -EALREADY)
	{
		return -EIO;
	}

	const nrfx_power_usbevt_config_t evt_cfg = { .handler = on_usb_evt };
	nrfx_power_usbevt_init(&evt_cfg);
	nrfx_power_usbevt_enable();
	return 0;
}

battery::State battery::read()
{
	int32_t ext_raw, int_raw;
	if (read_cell_mv(ch_ext, 4, &ext_raw) < 0 || read_cell_mv(ch_int, 5, &int_raw) < 0)
	{
		printk("Battery: ADC read failed, holding %u%%\n", state.pct);
		return state;
	}

	// Charging is a step event (USB plugged in): detect it on the raw, instantaneous
	// voltages so the bolt appears at once instead of after the EMA catches up. This
	// is the only use of the internal channel; its voltage goes no further.
	state.charging = (int_raw - ext_raw) > CHARGE_DETECT_MV;
	state.pct = mv_to_percent(ext_mv_ema.update(ext_raw));

	if (state.charging || state.pct >= LOW_EXIT_PCT)
	{
		state.low = false;
	}
	else if (state.pct <= LOW_ENTER_PCT)
	{
		state.low = true;
	}
	return state;
}
