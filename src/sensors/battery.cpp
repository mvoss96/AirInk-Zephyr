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
	// Whoever wants waking the moment the cable moves. Null until watch_supply().
	struct k_sem *wake_sem;

	/** The SoC saw VBUS appear or disappear. Runs in the POWER interrupt (shared with the clock
	 * driver), so it does the one thing an ISR may: give the semaphore. */
	void on_usb_evt(nrfx_power_usb_evt_t event)
	{
		// READY means the USB regulator settled; the cable did not move, so nothing to say.
		if (event == NRFX_POWER_USB_EVT_READY || wake_sem == nullptr)
		{
			return;
		}
		k_sem_give(wake_sem);
	}

	// [0] = external divider (P0.31, x4), [1] = internal VDDH/5 (x5).
	const struct adc_dt_spec ch_ext = ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 0);
	const struct adc_dt_spec ch_int = ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 1);

	/* USB is present (charging) when the supply rail (VDDH) sits well above the
	 * battery terminal (BAT+). On battery only, the two are ~equal; on USB, VDDH is
	 * the ~4.8 V USB rail, hundreds of mV above the cell. */
	constexpr int32_t CHARGE_DETECT_MV = 300;

	/** Read one channel as a cell voltage: raw counts x divider ratio, scaled BEFORE converting --
	 * adc_raw_to_millivolts_dt() truncates to whole mV, and converting first would multiply that
	 * error by `scale` (4 mV = two percentage points on the steep part of the Li-Ion curve).
	 * @param scale 4 for the external divider, 5 for VDDH/5
	 * @retval -errno conversion failed; *mv_out untouched */
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

	// Filter the mV, not the derived % (keeps sub-percent resolution). Asymmetric: the gauge falls
	// ~4x more readily than it rises, because a cell that reads low is pessimistic and one that
	// reads high strands the user. Charging is detected on the raw reading, so nothing here is slow.
	Ema<5, 3> ext_mv_ema;

	// Survives a failed conversion, so a hiccup neither zeroes the gauge nor flaps the
	// latch. Zero-initialized: at boot the first successful read fills it in.
	battery::State state;
	bool pct_seeded;

	/* Deadband on the reported percent. The +-1 pp dither is structural -- the ADC's LSB (~3.5 mV)
	 * is wider than the curve's ~2 mV/percent, and no mV smoothing can make the quantiser coarser
	 * than itself -- and every dither is a changed displayed value, i.e. a ~3 mAs refresh. 2 pp is
	 * wider than the dither, narrower than anyone would miss. */
	constexpr int PCT_DEADBAND = 2;

	/* Low-battery latch: the gap absorbs the +-1 pp jitter (a flapping latch would be a
	 * full-refresh black flash every 30 s); charging always releases it. */
	constexpr uint8_t LOW_ENTER_PCT = 5;
	constexpr uint8_t LOW_EXIT_PCT = 8;

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

int battery::watch_supply(struct k_sem *wake)
{
	wake_sem = wake;

	/* nrfx_power_init() WRITES DCDCEN from its config. Zephyr already enabled the DC/DC converters,
	 * and a zeroed config would switch them back off -- hundreds of microamps of regression. So
	 * read the state out of the peripheral and hand it back unchanged. */
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

	// Signed, because the comparison is what the deadband is: an unsigned `state.pct - 1` at
	// 0 % would wrap to 255 and the gauge would stick there forever.
	const int fresh_pct = mv_to_percent(ext_mv_ema.update(ext_raw));
	if (!pct_seeded)
	{
		state.pct = (uint8_t)fresh_pct;
		pct_seeded = true;
	}
	else if (fresh_pct >= (int)state.pct + PCT_DEADBAND || fresh_pct <= (int)state.pct - PCT_DEADBAND)
	{
		state.pct = (uint8_t)fresh_pct;
	}

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
