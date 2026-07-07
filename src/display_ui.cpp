#include "display_ui.hpp"
#include "ui_platform.hpp"

#include <stdio.h>
#include <stdlib.h>
#include <lvgl.h>

namespace {

/* One full-screen container per screen; only one is un-hidden at a time. */
lv_obj_t *boot_scr;
lv_obj_t *sensor_scr;
lv_obj_t *error_scr;

/* Value labels updated at runtime. */
lv_obj_t *co2_value;
lv_obj_t *hum_value;
lv_obj_t *temp_value;
lv_obj_t *err_title_lbl;
lv_obj_t *err_detail_lbl;

/* Landscape canvas (rotation=270). The driver's get_capabilities swaps x/y,
 * so LVGL renders a full 400x300 (source axis horizontal, gate axis vertical).
 * No clip needed in this orientation. */
#define SCR_W 400
#define SCR_H 300

lv_obj_t *make_container(lv_obj_t *parent)
{
	lv_obj_t *c = lv_obj_create(parent);
	lv_obj_remove_style_all(c);
	lv_obj_set_size(c, SCR_W, SCR_H);
	lv_obj_set_pos(c, 0, 0);
	lv_obj_set_style_bg_color(c, lv_color_white(), 0);
	lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
	lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
	return c;
}

lv_obj_t *make_label(lv_obj_t *parent, const lv_font_t *font, lv_coord_t w)
{
	lv_obj_t *l = lv_label_create(parent);
	lv_obj_set_style_text_font(l, font, 0);
	lv_obj_set_style_text_color(l, lv_color_black(), 0);
	lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
	if (w) {
		lv_obj_set_width(l, w);
	}
	return l;
}

lv_obj_t *make_divider(lv_obj_t *parent, lv_coord_t w, lv_coord_t h)
{
	lv_obj_t *d = lv_obj_create(parent);
	lv_obj_remove_style_all(d);
	lv_obj_set_size(d, w, h);
	lv_obj_set_style_bg_color(d, lv_color_black(), 0);
	lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
	return d;
}

/*
 * Push the current LVGL frame to the panel. `full` selects a full refresh
 * (clean, but a black flash) vs a partial refresh (fast, only changed pixels,
 * no flash). The ssd16xx driver does a partial refresh whenever blanking is
 * off and a partial{} profile exists; wrapping the flush in blanking on/off
 * forces a full refresh instead.
 */
void flush(bool full)
{
	lv_display_t *disp = lv_display_get_default();

	if (full) {
		plat::blanking_on();             /* select the full-refresh profile */
		lv_refr_now(disp);               /* write RAM (no refresh yet) */
		plat::blanking_off();            /* trigger the full refresh */
	} else {
		lv_refr_now(disp);               /* partial refresh */
	}
}

void show_only(lv_obj_t *which, bool full)
{
	lv_obj_add_flag(boot_scr, LV_OBJ_FLAG_HIDDEN);
	lv_obj_add_flag(sensor_scr, LV_OBJ_FLAG_HIDDEN);
	lv_obj_add_flag(error_scr, LV_OBJ_FLAG_HIDDEN);
	lv_obj_clear_flag(which, LV_OBJ_FLAG_HIDDEN);
	flush(full);
}

/* Refresh bookkeeping. */
enum ScreenId { SCREEN_NONE, SCREEN_BOOT, SCREEN_SENSOR, SCREEN_ERROR };
ScreenId cur_screen = SCREEN_NONE;

/* Force a full refresh every N partial updates to clear e-paper ghosting. */
constexpr int FULL_REFRESH_EVERY = 12;
int partials_since_full;

/* Last shown reading, to skip refreshes when nothing changed. */
bool have_last_reading;
uint16_t last_co2;
int32_t last_temp_x100;
uint16_t last_hum_x100;

} // namespace

int ui::init()
{
	if (!plat::display_ready()) {
		plat::log("Display not ready\n");
		return -1;
	}

	lv_obj_t *scr = lv_scr_act();
	lv_obj_set_style_bg_color(scr, lv_color_white(), 0);
	lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

	/* ---- boot screen ---- */
	boot_scr = make_container(scr);
	lv_obj_t *word = make_label(boot_scr, &lv_font_montserrat_48, SCR_W);
	lv_label_set_text(word, "AirInk");
	lv_obj_align(word, LV_ALIGN_CENTER, 0, -30);
	lv_obj_t *rule = make_divider(boot_scr, 180, 2);
	lv_obj_align(rule, LV_ALIGN_CENTER, 0, 6);
	lv_obj_t *sub = make_label(boot_scr, &lv_font_montserrat_16, SCR_W);
	lv_label_set_text(sub, "Air Quality Monitor");
	lv_obj_align(sub, LV_ALIGN_CENTER, 0, 34);
	lv_obj_t *foot = make_label(boot_scr, &lv_font_montserrat_14, SCR_W);
	lv_label_set_text(foot, "nRF52840 - Zephyr");
	lv_obj_align(foot, LV_ALIGN_BOTTOM_MID, 0, -8);

	/*
	 * ---- sensor screen: CO2 on top, Humidity | Temperature below ----
	 *
	 * Every metric is a flex-column cell: big value on top, small label
	 * underneath, both centred vertically and horizontally in the cell. So the
	 * value dominates and the caption sits below it, centred in its region.
	 */
	sensor_scr = make_container(scr);

	auto make_metric = [&](lv_align_t align, int y, int w, int h,
			       const char *label_txt) -> lv_obj_t * {
		lv_obj_t *cell = lv_obj_create(sensor_scr);
		lv_obj_remove_style_all(cell);
		lv_obj_set_size(cell, w, h);
		lv_obj_align(cell, align, 0, y);
		lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
		lv_obj_set_flex_flow(cell, LV_FLEX_FLOW_COLUMN);
		lv_obj_set_flex_align(cell, LV_FLEX_ALIGN_CENTER,
				      LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
		lv_obj_set_style_pad_row(cell, 2, 0);

		lv_obj_t *val = make_label(cell, &lv_font_montserrat_48, w);
		lv_obj_t *lbl = make_label(cell, &lv_font_montserrat_16, w);
		lv_label_set_text(lbl, label_txt);
		return val;
	};

	constexpr int SPLIT_Y = 150;

	/* CO2: full-width cell across the top half. */
	co2_value = make_metric(LV_ALIGN_TOP_MID, 0, SCR_W, SPLIT_Y, "CO2");
	lv_label_set_text(co2_value, "-- ppm");

	/* Divider between the CO2 header and the bottom half. */
	lv_obj_t *hdiv = make_divider(sensor_scr, SCR_W - 4, 2);
	lv_obj_align(hdiv, LV_ALIGN_TOP_MID, 0, SPLIT_Y);
	lv_obj_t *vdiv = make_divider(sensor_scr, 2, SCR_H - (SPLIT_Y + 4));
	lv_obj_align(vdiv, LV_ALIGN_BOTTOM_MID, 0, -2);

	/* Bottom half: Humidity (left) | Temperature (right). */
	const int cell_top = SPLIT_Y + 2;
	const int cell_h = SCR_H - cell_top;
	const int cell_w = SCR_W / 2;

	hum_value = make_metric(LV_ALIGN_TOP_LEFT, cell_top, cell_w, cell_h, "Humidity");
	lv_label_set_text(hum_value, "-- %");

	temp_value = make_metric(LV_ALIGN_TOP_RIGHT, cell_top, cell_w, cell_h, "Temperature");
	lv_label_set_text(temp_value, "-- C");

	/* ---- error screen ---- */
	error_scr = make_container(scr);
	err_title_lbl = make_label(error_scr, &lv_font_montserrat_28, SCR_W);
	lv_label_set_text(err_title_lbl, "SENSOR ERROR");
	lv_obj_align(err_title_lbl, LV_ALIGN_CENTER, 0, -20);
	err_detail_lbl = make_label(error_scr, &lv_font_montserrat_16, SCR_W);
	lv_label_set_text(err_detail_lbl, "");
	lv_obj_align(err_detail_lbl, LV_ALIGN_CENTER, 0, 20);

	show_only(boot_scr, true); /* boot splash: full refresh */
	cur_screen = SCREEN_BOOT;
	return 0;
}

void ui::show_boot()
{
	show_only(boot_scr, true);
	cur_screen = SCREEN_BOOT;
}

void ui::show_reading(uint16_t co2_ppm, int32_t temp_c_x100, uint16_t hum_x100)
{
	/* Nothing changed and already on the sensor screen -> don't refresh. */
	if (cur_screen == SCREEN_SENSOR && have_last_reading &&
	    co2_ppm == last_co2 && temp_c_x100 == last_temp_x100 &&
	    hum_x100 == last_hum_x100) {
		return;
	}

	char buf[24];

	snprintf(buf, sizeof(buf), "%u ppm", co2_ppm);
	lv_label_set_text(co2_value, buf);

	snprintf(buf, sizeof(buf), "%u %%", (unsigned)(hum_x100 / 100));
	lv_label_set_text(hum_value, buf);

	const int whole = temp_c_x100 / 100;
	const int frac = abs(temp_c_x100 % 100) / 10;
	snprintf(buf, sizeof(buf), "%d.%d C", whole, frac);
	lv_label_set_text(temp_value, buf);

	/* Full refresh on the first sensor frame (screen change) and periodically
	 * to clear ghosting; partial (fast, no flash) for ordinary value updates. */
	const bool screen_changed = (cur_screen != SCREEN_SENSOR);
	const bool full = screen_changed || partials_since_full >= FULL_REFRESH_EVERY;

	show_only(sensor_scr, full);

	cur_screen = SCREEN_SENSOR;
	partials_since_full = full ? 0 : partials_since_full + 1;
	last_co2 = co2_ppm;
	last_temp_x100 = temp_c_x100;
	last_hum_x100 = hum_x100;
	have_last_reading = true;
}

void ui::show_error(const char *title, const char *detail)
{
	if (title) {
		lv_label_set_text(err_title_lbl, title);
	}
	lv_label_set_text(err_detail_lbl, detail ? detail : "");
	show_only(error_scr, true); /* error: full refresh */
	cur_screen = SCREEN_ERROR;
	have_last_reading = false;
}
