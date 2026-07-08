/*
 * Partial-refresh test harness (built INSTEAD of main.cpp -- see CMakeLists.txt).
 *
 * Isolates the e-paper refresh path from the app: shows a counter that increments
 * once per second. The first paint after boot is a FULL refresh (blanking on/off =
 * full LUT); every update after that is a PARTIAL refresh (bare lv_refr_now = the
 * driver's partial/fast-LUT path). Watch the panel for ghosting, and measure each
 * partial on the PPK2 -- no sensor / measurement / PM code in the way.
 *
 * The console UART is left running (not idle-suspended) so COM9 streams a line per
 * second. Swap back to src/main.cpp in CMakeLists for the real app.
 */
#include <zephyr/kernel.h>
#include <stdio.h>
#include <lvgl.h>

#include "ui/ui_platform.hpp"

extern "C" {
extern const lv_font_t b612_48;
}

int main(void)
{
	printk("partial-test: start\n");

	if (!plat::display_ready()) {
		printk("partial-test: display not ready\n");
		return 0;
	}

	lv_display_t *disp = lv_display_get_default();

	lv_obj_t *scr = lv_scr_act();
	lv_obj_set_style_bg_color(scr, lv_color_white(), 0);
	lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

	lv_obj_t *label = lv_label_create(scr);
	lv_obj_set_style_text_font(label, &b612_48, 0);
	lv_obj_set_style_text_color(label, lv_color_black(), 0);
	lv_label_set_text(label, "0");
	lv_obj_center(label);

	/* First paint: FULL refresh (blanking on/off selects the full LUT). */
	plat::blanking_on();
	lv_refr_now(disp);
	plat::blanking_off();
	printk("partial-test: boot FULL refresh done\n");

	char buf[16];
	uint32_t n = 0;
	while (1) {
		k_msleep(1000);
		n++;
		snprintf(buf, sizeof(buf), "%u", (unsigned)n);
		lv_label_set_text(label, buf);

		/* Every 10th = FULL (blanking on/off), else PARTIAL (bare lv_refr_now).
		 * No sensor code -> isolates pure e-paper refresh energy on the PPK2. */
		bool full = (n % 10 == 0);
		if (full) {
			plat::blanking_on();
			lv_refr_now(disp);
			plat::blanking_off();
		} else {
			lv_refr_now(disp);
		}
		printk("partial-test: n=%u %s\n", (unsigned)n, full ? "FULL" : "PARTIAL");
	}
	return 0;
}
