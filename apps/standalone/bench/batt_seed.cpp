/** @file
 * Diagnostic: why does the battery gauge's seed scatter ~26 mV between boots?
 *
 * Two candidates:
 *
 *   (1) the first SAADC conversion after channel setup is an outlier, or the readings
 *       are simply noisy one at a time;
 *   (2) the boot splash's full panel refresh (~26 mA for ~2 s) still loads the cell
 *       when the first sample is taken, so it reads low and recovers afterwards.
 *
 * Burst-read the raw cell voltage right after the splash, then again as the cell
 * recovers. The spread WITHIN a burst answers (1); the drift BETWEEN bursts answers
 * (2). Prints every sample so nothing is hidden behind a mean.
 *
 *   west build -b promicro_nrf52840/nrf52840 apps/standalone -p always -- -DAPP_ENTRY=batt_seed
 */
#include <zephyr/kernel.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/devicetree.h>
#include <stdint.h>

#include "ui/display_ui.hpp"

namespace
{
	// [0] = external divider (P0.31, x4) -- the gauge channel.
	const struct adc_dt_spec ch_ext = ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 0);

	constexpr int BURST = 8;

	/** Read the gauge channel once, scaled to the cell.
	 *
	 * @param[out] mv_out receives the cell voltage in millivolts
	 * @retval 0      on success
	 * @retval -errno the conversion failed
	 */
	int read_cell_mv(int32_t *mv_out)
	{
		int16_t raw = 0;
		struct adc_sequence seq = {
			.buffer = &raw,
			.buffer_size = sizeof(raw),
		};

		int err = adc_sequence_init_dt(&ch_ext, &seq);
		if (err == 0)
		{
			err = adc_read(ch_ext.dev, &seq);
		}
		if (err < 0)
		{
			return err;
		}

		// Pre-scale by the divider ratio: converting first would round to whole mV at
		// the pin and then multiply that error by 4. Mirrors battery.cpp.
		int32_t mv = (int32_t)raw * 4;
		err = adc_raw_to_millivolts_dt(&ch_ext, &mv);
		if (err < 0)
		{
			return err;
		}
		*mv_out = mv;
		return 0;
	}

	/** Take BURST samples spaced `gap_ms` apart and print every one of them.
	 * Lets the divider node sit idle for 2 s first. If the reading depends on the gap,
	 * the sample-and-hold is draining the ~75k divider and it recovers between
	 * conversions -- then the FIRST (rested) sample is the true one, and averaging
	 * back-to-back reads would bias us low. If instead only the very first conversion
	 * stands out regardless of gap, it is a wake-up offset and the later samples are
	 * the true ones.
	 *
	 * @param tag    label for the printed line, e.g. "gap=5ms"
	 * @param gap_ms delay between consecutive conversions; 0 for back-to-back
	 */
	void burst(const char *tag, int gap_ms)
	{
		int32_t mv[BURST];
		int32_t lo = INT32_MAX, hi = INT32_MIN;
		int32_t sum = 0;

		k_sleep(K_SECONDS(2)); // let the divider node settle fully

		for (int i = 0; i < BURST; i++)
		{
			if (i && gap_ms)
			{
				k_sleep(K_MSEC(gap_ms));
			}
			if (read_cell_mv(&mv[i]) < 0)
			{
				printk("%s: adc read failed\n", tag);
				return;
			}
			sum += mv[i];
			lo = (mv[i] < lo) ? mv[i] : lo;
			hi = (mv[i] > hi) ? mv[i] : hi;
		}

		// rest[] = mean of samples 2..N, i.e. what averaging would give us.
		const int32_t rest = (sum - mv[0]) / (BURST - 1);
		printk("%-9s first %4d  rest %4d  delta %+3d  spread %2d  |",
			   tag, mv[0], rest, mv[0] - rest, hi - lo);
		for (int i = 0; i < BURST; i++)
		{
			printk(" %d", mv[i]);
		}
		printk("\n");
	}

} // namespace

int main(void)
{
	printk("\n=== battery seed diagnostic ===\n");

	// Reproduces the real boot order: the splash renders a full refresh, and only
	// then does battery::init() run and take its first (seeding) sample.
	printk("display %s\n", (ui::init() == 0) ? "ok" : "FAILED");

	if (!adc_is_ready_dt(&ch_ext))
	{
		printk("adc not ready\n");
		return 0;
	}
	adc_channel_setup_dt(&ch_ext);

	// Sweep the spacing between conversions. `delta` is what the first sample gains
	// over the average of the rest -- if it shrinks as the gap grows, the divider is
	// being drained and needs recovery time, not the ADC.
	burst("gap=0ms", 0);
	burst("gap=1ms", 1);
	burst("gap=5ms", 5);
	burst("gap=20ms", 20);
	burst("gap=100ms", 100);
	burst("gap=500ms", 500);

	printk("=== done ===\n");
	return 0;
}
