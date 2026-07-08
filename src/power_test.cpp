/*
 * Power characterization harness (built INSTEAD of main.cpp -- see CMakeLists.txt).
 * Isolates one consumer at a time so each can be measured on the PPK2. Pick the
 * test with TEST_MODE below, rebuild, flash, measure. NOT part of the shipped app.
 */
#include <zephyr/kernel.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/device.h>
#include <zephyr/pm/device.h>
#include <stdio.h>
#include <lvgl.h>

#include "ui/ui_platform.hpp"
#include "sensors/scd41.hpp"

extern "C" {
extern const lv_font_t b612_48;
extern const lv_font_t dseg7_48;
}

/* ---- pick the test (change per build) ---- */
#define MODE_SCD_CO2        1  /* full single-shot CO2+T+RH, ~5 s */
#define MODE_SCD_RHT        2  /* raw measure_single_shot_rht_only (0x2196), ~50 ms */
#define MODE_EPD_FULL       3  /* full refresh of a busy 4-number screen each second */
#define MODE_EPD_PARTIAL    4  /* partial refresh, all 4 numbers change each second */
#define MODE_IDLE           5  /* console UART suspended, sleep forever (idle floor) */
#define TEST_MODE           MODE_IDLE

static const struct i2c_dt_spec scd_bus = I2C_DT_SPEC_GET(DT_NODELABEL(scd41));
static const struct device *const console_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));

static void scd_wake()
{
	const uint8_t cmd[2] = {0x36, 0xF6};
	i2c_write_dt(&scd_bus, cmd, 2);
	i2c_write_dt(&scd_bus, cmd, 2);
	k_msleep(30);
}

#if TEST_MODE == MODE_EPD_FULL || TEST_MODE == MODE_EPD_PARTIAL
static lv_obj_t *make_num(lv_obj_t *scr, int x, int y)
{
	lv_obj_t *l = lv_label_create(scr);
	lv_obj_set_style_text_font(l, &dseg7_48, 0);
	lv_obj_set_style_text_color(l, lv_color_black(), 0);
	lv_label_set_text(l, "000");
	lv_obj_set_pos(l, x, y);
	return l;
}
#endif

int main(void)
{
	printk("power-test: mode %d start\n", TEST_MODE);

#if TEST_MODE == MODE_SCD_CO2
	scd41::init();
	Scd41Reading r;
	while (1) {
		k_msleep(3000);
		int64_t t0 = k_uptime_get();
		int rc = scd41::sample(&r);
		printk("SCD_CO2: rc=%d dt=%ums co2=%u\n", rc,
		       (unsigned)(k_uptime_get() - t0), r.co2_ppm);
	}

#elif TEST_MODE == MODE_SCD_RHT
	/* raw measure_single_shot_rht_only: wake -> 0x2196 -> wait -> read 0xEC05 ->
	 * power down (0x36E0). Skips the CO2 photoacoustic measurement. */
	while (1) {
		k_msleep(3000);
		int64_t t0 = k_uptime_get();
		scd_wake();
		const uint8_t meas[2] = {0x21, 0x96};
		i2c_write_dt(&scd_bus, meas, 2);
		k_msleep(60);
		const uint8_t rdcmd[2] = {0xEC, 0x05};
		uint8_t rd[9];
		i2c_write_dt(&scd_bus, rdcmd, 2);
		i2c_read_dt(&scd_bus, rd, sizeof(rd));
		const uint8_t pd[2] = {0x36, 0xE0}; /* power down */
		i2c_write_dt(&scd_bus, pd, 2);
		printk("SCD_RHT: dt=%ums t_raw=%02x%02x\n",
		       (unsigned)(k_uptime_get() - t0), rd[3], rd[4]);
	}

#elif TEST_MODE == MODE_EPD_FULL || TEST_MODE == MODE_EPD_PARTIAL
	if (!plat::display_ready()) { printk("display not ready\n"); return 0; }
	lv_display_t *disp = lv_display_get_default();
	lv_obj_t *scr = lv_scr_act();
	lv_obj_set_style_bg_color(scr, lv_color_white(), 0);
	lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
	lv_obj_t *a = make_num(scr, 40, 10);
	lv_obj_t *b = make_num(scr, 40, 90);
	lv_obj_t *c = make_num(scr, 40, 170);
	lv_obj_t *d = make_num(scr, 40, 240);
	plat::blanking_on(); lv_refr_now(disp); plat::blanking_off();
	printk("EPD: boot full done\n");
	char buf[8];
	uint32_t n = 0;
	while (1) {
		k_msleep(1000);
		n++;
		snprintf(buf, sizeof(buf), "%u", (unsigned)(n % 1000));
		lv_label_set_text(a, buf); lv_label_set_text(b, buf);
		lv_label_set_text(c, buf); lv_label_set_text(d, buf);
#if TEST_MODE == MODE_EPD_FULL
		plat::blanking_on(); lv_refr_now(disp); plat::blanking_off();
		printk("EPD_FULL n=%u\n", (unsigned)n);
#else
		lv_refr_now(disp);
		printk("EPD_PARTIAL n=%u\n", (unsigned)n);
#endif
	}

#else /* MODE_IDLE */
	printk("IDLE: suspending console UART, sleeping\n");
	k_msleep(200);
	pm_device_action_run(console_dev, PM_DEVICE_ACTION_SUSPEND);
	while (1) {
		k_msleep(60000);
	}
#endif
	return 0;
}
