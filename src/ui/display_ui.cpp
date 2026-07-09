#include "display_ui.hpp"
#include "ui_platform.hpp"
#include "../version.hpp"

#include <stdio.h>
#include <stdlib.h>
#include <lvgl.h>

/*
 * AirInk UI: one flat file. A persistent status bar (battery +
 * link) sits at the top and is never hidden; below it, one content view (boot /
 * sensor / error) is visible at a time. Each view is a lightweight container so
 * a view switch is a single hide/show — no screen enum, model, or registry.
 *
 * E-paper refresh: full (blanking on/off -> clean, black flash) on a view change
 * and periodically to clear ghosting; partial (fast, no flash) for in-place value
 * and status-bar updates. See flush()/refresh() and plat::blanking_*.
 */

/* UI fonts (generated into src/fonts/, bpp=1). C linkage.
 * B612 for all text; DSEG7 (7-segment, digits only) for the big sensor values. */
extern "C"
{
	extern const lv_font_t b612_48, b612_28, b612_16, b612_14;
	extern const lv_font_t dseg7_48, dseg7_18;
}

namespace
{
	/* Landscape canvas (rotation=270); the driver swaps x/y so LVGL renders 400x300.
	 * The status bar takes the top STATUS_H px; content views fill the rest. */
	constexpr int SCR_W = 400, SCR_H = 300;
	constexpr int STATUS_H = 28;
	constexpr int CONTENT_Y = STATUS_H, CONTENT_W = SCR_W, CONTENT_H = SCR_H - STATUS_H;
	constexpr int FULL_REFRESH_EVERY = 100;

	/* Status bar (never hidden). */
	lv_obj_t *status_bar;
	lv_obj_t *batt_frame, *batt_fill, *batt_bolt, *batt_pct_lbl, *link_lbl;
	int last_charging = -1;

	/* Charging bolt as a filled 1bpp image (I1: index0 = black bolt, index1 =
	 * white bg, which is invisible on the white status bar). Shown in place of
	 * the percentage while charging. 12x16, generated from ASCII art. */
	const uint8_t bolt_map[] = {
		0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, /* I1 palette: black, white */
		0xff, 0xff,
		0xfd, 0xff,
		0xf9, 0xff,
		0xf1, 0xff,
		0xf3, 0xff,
		0xe3, 0xff,
		0xc3, 0xff,
		0xc0, 0x3f,
		0x80, 0x7f,
		0x00, 0x7f,
		0xf0, 0xff,
		0xf1, 0xff,
		0xe3, 0xff,
		0xe7, 0xff,
		0xef, 0xff,
		0xdf, 0xff,
		0xff, 0xff,
		0xff, 0xff,
	};
	const lv_image_dsc_t bolt_img = {
		.header = {
			.magic = LV_IMAGE_HEADER_MAGIC,
			.cf = LV_COLOR_FORMAT_I1,
			.flags = 0,
			.w = 13,
			.h = 18,
			.stride = 2,
			.reserved_2 = 0,
		},
		.data_size = sizeof(bolt_map),
		.data = bolt_map,
		.reserved = NULL,
	};

	/* Content views (exactly one un-hidden at a time). */
	lv_obj_t *boot_root;
	lv_obj_t *sensor_root, *co2_value, *hum_value, *temp_value;
	lv_obj_t *error_root, *err_title_lbl, *err_detail_lbl;
	lv_obj_t *lowbat_root, *lowbat_fill, *lowbat_pct_lbl;
	lv_obj_t *reset_root, *reset_seconds_lbl;

	/* Views + refresh bookkeeping. Setters stage `pending_view` and dirty the
	 * widgets; ui::refresh() commits: hide/show the view + ONE panel refresh. */
	enum View
	{
		VIEW_NONE,
		VIEW_BOOT,
		VIEW_SENSOR,
		VIEW_ERROR,
		VIEW_LOWBAT,
		VIEW_RESET
	};
	View shown_view = VIEW_NONE;   /* what the panel currently shows */
	View pending_view = VIEW_BOOT; /* staged by a set_<view>; committed by refresh() */
	bool dirty;                    /* a setter changed something since the last refresh */
	int partials_since_full;
	bool ready; /* init() built the widgets; every entry point no-ops until then */

	/* Skip-refresh dedup. The last_* sentinels are int, not the API's uint8_t,
	 * because they use -1 for "nothing shown yet". */
	bool have_last_reading;
	uint16_t last_co2_ppm, last_hum_x100;
	int32_t last_temp_x100;
	int last_batt_pct = -1;
	int last_link = -1;
	int last_lowbat_pct = -1;
	int last_reset_seconds = -1;

	/* ---- widget helpers ---- */

	lv_obj_t *make_label(lv_obj_t *parent, const lv_font_t *font, lv_coord_t w)
	{
		lv_obj_t *l = lv_label_create(parent);
		lv_obj_set_style_text_font(l, font, 0);
		lv_obj_set_style_text_color(l, lv_color_black(), 0);
		lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
		if (w)
		{
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

	/* A full-width white content container at y=CONTENT_Y (below the status bar). */
	lv_obj_t *make_view(lv_obj_t *parent)
	{
		lv_obj_t *c = lv_obj_create(parent);
		lv_obj_remove_style_all(c);
		lv_obj_set_size(c, CONTENT_W, CONTENT_H);
		lv_obj_set_pos(c, 0, CONTENT_Y);
		lv_obj_set_style_bg_color(c, lv_color_white(), 0);
		lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
		lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
		return c;
	}

	/*
	 * Push the current LVGL frame to the panel. `full` selects a full refresh (clean,
	 * but a black flash) vs a partial refresh (fast, only changed pixels, no flash).
	 * The ssd16xx driver does a partial refresh whenever blanking is off; wrapping the
	 * flush in blanking on/off forces a full refresh instead.
	 */
	void flush(bool full)
	{
		lv_display_t *disp = lv_display_get_default();

		/* Wake the panel for the whole refresh; the full-refresh update fires in
		 * blanking_off(), so suspend only after that. */
		plat::display_resume();
		if (full)
		{
			plat::blanking_on();  /* select the full-refresh profile */
			lv_refr_now(disp);	  /* write RAM (no refresh yet) */
			plat::blanking_off(); /* trigger the full refresh */
		}
		else
		{
			lv_refr_now(disp); /* partial refresh */
		}
		plat::display_suspend(); /* deep-sleep the panel until the next refresh */
	}

	void hide_all_content()
	{
		lv_obj_add_flag(boot_root, LV_OBJ_FLAG_HIDDEN);
		lv_obj_add_flag(sensor_root, LV_OBJ_FLAG_HIDDEN);
		lv_obj_add_flag(error_root, LV_OBJ_FLAG_HIDDEN);
		lv_obj_add_flag(lowbat_root, LV_OBJ_FLAG_HIDDEN);
		lv_obj_add_flag(reset_root, LV_OBJ_FLAG_HIDDEN);
	}

	lv_obj_t *root_for(View v)
	{
		switch (v)
		{
		case VIEW_SENSOR: return sensor_root;
		case VIEW_ERROR:  return error_root;
		case VIEW_LOWBAT: return lowbat_root;
		case VIEW_RESET:  return reset_root;
		default:          return boot_root;
		}
	}

	const char *link_token(ui::Link state)
	{
		switch (state)
		{
		case ui::Link::BleAdv:
			return "BLE..";
		case ui::Link::BleConnected:
			return "BLE";
		case ui::Link::ZigbeeJoining:
			return "ZB..";
		case ui::Link::ZigbeeConnected:
			return "ZB";
		case ui::Link::None:
			break;
		}
		return "--";
	}

	/* ---- builders (once, in init) ---- */

	void build_status_bar(lv_obj_t *scr)
	{
		status_bar = lv_obj_create(scr);
		lv_obj_remove_style_all(status_bar);
		lv_obj_set_size(status_bar, SCR_W, STATUS_H);
		lv_obj_set_pos(status_bar, 0, 0);
		lv_obj_set_style_bg_color(status_bar, lv_color_white(), 0);
		lv_obj_set_style_bg_opa(status_bar, LV_OPA_COVER, 0);
		lv_obj_clear_flag(status_bar, LV_OBJ_FLAG_SCROLLABLE);

		/* Battery: small outline frame + proportional fill + a nub on the right.
		 * No percentage text — DSEG7 has no '%' glyph and only exists at 48px, so
		 * the fill level is the sole (glanceable) indicator. */
		batt_frame = lv_obj_create(status_bar);
		lv_obj_remove_style_all(batt_frame);
		lv_obj_set_size(batt_frame, 24, 13);
		lv_obj_align(batt_frame, LV_ALIGN_LEFT_MID, 6, 0);
		lv_obj_set_style_border_color(batt_frame, lv_color_black(), 0);
		lv_obj_set_style_border_width(batt_frame, 1, 0);
		lv_obj_clear_flag(batt_frame, LV_OBJ_FLAG_SCROLLABLE);

		lv_obj_t *nub = make_divider(status_bar, 2, 6);
		lv_obj_align_to(nub, batt_frame, LV_ALIGN_OUT_RIGHT_MID, 0, 0);

		batt_fill = lv_obj_create(batt_frame);
		lv_obj_remove_style_all(batt_fill);
		lv_obj_set_size(batt_fill, 0, 9); /* width set in set_battery() */
		lv_obj_align(batt_fill, LV_ALIGN_LEFT_MID, 1, 0);
		lv_obj_set_style_bg_color(batt_fill, lv_color_black(), 0);
		lv_obj_set_style_bg_opa(batt_fill, LV_OPA_COVER, 0);

		/* Percentage in DSEG7 (digits only, no '%') to match the sensor values. */
		batt_pct_lbl = make_label(status_bar, &dseg7_18, 0);
		lv_obj_align_to(batt_pct_lbl, batt_frame, LV_ALIGN_OUT_RIGHT_MID, 10, 0);
		lv_label_set_text(batt_pct_lbl, "");

		/* Charging bolt, shown in place of the percentage while USB is present
		 * (see set_battery()). Same anchor as the number. */
		batt_bolt = lv_image_create(status_bar);
		lv_image_set_src(batt_bolt, &bolt_img);
		lv_obj_align_to(batt_bolt, batt_frame, LV_ALIGN_OUT_RIGHT_MID, 10, 0);
		lv_obj_add_flag(batt_bolt, LV_OBJ_FLAG_HIDDEN);

		/* Link: short text token, right-aligned. */
		link_lbl = make_label(status_bar, &b612_14, 0);
		lv_obj_align(link_lbl, LV_ALIGN_RIGHT_MID, -6, 0);
		lv_label_set_text(link_lbl, link_token(ui::Link::None));

		/* Divider under the bar. */
		lv_obj_t *sep = make_divider(status_bar, SCR_W, 1);
		lv_obj_align(sep, LV_ALIGN_BOTTOM_MID, 0, 0);
	}

	void build_boot(lv_obj_t *scr)
	{
		boot_root = make_view(scr);

		lv_obj_t *logo = make_label(boot_root, &b612_48, CONTENT_W);
		lv_label_set_text(logo, "AirInk");
		lv_obj_align(logo, LV_ALIGN_CENTER, 0, -24);

		lv_obj_t *rule = make_divider(boot_root, 180, 2);
		lv_obj_align(rule, LV_ALIGN_CENTER, 0, 10);

		lv_obj_t *sub = make_label(boot_root, &b612_16, CONTENT_W);
		lv_label_set_text(sub, "Air Quality Monitor");
		lv_obj_align(sub, LV_ALIGN_CENTER, 0, 36);

		lv_obj_t *foot = make_label(boot_root, &b612_14, CONTENT_W);
		lv_label_set_text(foot, "by Marcus Voss");
		lv_obj_align(foot, LV_ALIGN_BOTTOM_MID, 0, -24);

		/* Firmware version + build date/time (compile-time). */
		lv_obj_t *build = make_label(boot_root, &b612_14, CONTENT_W);
		lv_label_set_text(build, "v" AIRINK_VERSION "  " __DATE__ "  " __TIME__);
		lv_obj_align(build, LV_ALIGN_BOTTOM_MID, 0, -6);
	}

	void build_sensor(lv_obj_t *scr)
	{
		sensor_root = make_view(scr);

		/* Each metric is a cell: big value + small unit on one baseline, caption
		 * pinned below. Returns the value label so callers can update it. */
		auto make_metric = [&](lv_align_t align, int y, int w, int h,
							   const char *unit, const char *name) -> lv_obj_t *
		{
			lv_obj_t *cell = lv_obj_create(sensor_root);
			lv_obj_remove_style_all(cell);
			lv_obj_set_size(cell, w, h);
			lv_obj_align(cell, align, 0, y);
			lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);

			lv_obj_t *row = lv_obj_create(cell);
			lv_obj_remove_style_all(row);
			lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
			lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
			lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
			lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER,
								  LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
			lv_obj_set_style_pad_column(row, 5, 0);

			lv_obj_t *val = make_label(row, &dseg7_48, 0);
			lv_obj_t *u = make_label(row, &b612_28, 0);
			lv_label_set_text(u, unit);
			/* FLEX_ALIGN_END aligns box bottoms, not text baselines; nudge the small
			 * unit up by the descent difference so the baselines line up. */
			lv_obj_set_style_translate_y(u, (lv_coord_t)(b612_28.base_line - dseg7_48.base_line), 0);
			lv_obj_align(row, LV_ALIGN_CENTER, 0, 0);

			lv_obj_t *nm = make_label(cell, &b612_16, w);
			lv_label_set_text(nm, name);
			lv_obj_align(nm, LV_ALIGN_BOTTOM_MID, 0, -6);
			return val;
		};

		constexpr int SPLIT_Y = CONTENT_H / 2; /* 136 */

		co2_value = make_metric(LV_ALIGN_TOP_MID, 0, CONTENT_W, SPLIT_Y, "ppm", "CO2");
		lv_label_set_text(co2_value, "--");

		lv_obj_t *hdiv = make_divider(sensor_root, CONTENT_W - 4, 2);
		lv_obj_align(hdiv, LV_ALIGN_TOP_MID, 0, SPLIT_Y);
		lv_obj_t *vdiv = make_divider(sensor_root, 2, CONTENT_H - (SPLIT_Y + 4));
		lv_obj_align(vdiv, LV_ALIGN_BOTTOM_MID, 0, -2);

		const int cell_top = SPLIT_Y + 2;
		const int cell_h = CONTENT_H - cell_top;
		const int cell_w = CONTENT_W / 2;

		hum_value = make_metric(LV_ALIGN_TOP_LEFT, cell_top, cell_w, cell_h, "%", "Humidity");
		lv_label_set_text(hum_value, "--");

		temp_value = make_metric(LV_ALIGN_TOP_RIGHT, cell_top, cell_w, cell_h, "\xC2\xB0"
																			   "C",
								 "Temperature");
		lv_label_set_text(temp_value, "--");
	}

	void build_error(lv_obj_t *scr)
	{
		error_root = make_view(scr);

		err_title_lbl = make_label(error_root, &b612_28, CONTENT_W);
		lv_label_set_text(err_title_lbl, "SENSOR ERROR");
		lv_obj_align(err_title_lbl, LV_ALIGN_CENTER, 0, -18);

		err_detail_lbl = make_label(error_root, &b612_16, CONTENT_W);
		lv_label_set_text(err_detail_lbl, "");
		lv_obj_align(err_detail_lbl, LV_ALIGN_CENTER, 0, 18);
	}

	void build_lowbat(lv_obj_t *scr)
	{
		lowbat_root = make_view(scr);

		/* Big battery outline + a small proportional fill + nub. */
		lv_obj_t *frame = lv_obj_create(lowbat_root);
		lv_obj_remove_style_all(frame);
		lv_obj_set_size(frame, 130, 60);
		lv_obj_align(frame, LV_ALIGN_CENTER, 0, -40);
		lv_obj_set_style_border_color(frame, lv_color_black(), 0);
		lv_obj_set_style_border_width(frame, 4, 0);
		lv_obj_clear_flag(frame, LV_OBJ_FLAG_SCROLLABLE);

		lv_obj_t *nub = make_divider(lowbat_root, 8, 26);
		lv_obj_align_to(nub, frame, LV_ALIGN_OUT_RIGHT_MID, 0, 0);

		lowbat_fill = lv_obj_create(frame);
		lv_obj_remove_style_all(lowbat_fill);
		lv_obj_set_size(lowbat_fill, 0, 44); /* width set in show_low_battery() */
		lv_obj_align(lowbat_fill, LV_ALIGN_LEFT_MID, 4, 0);
		lv_obj_set_style_bg_color(lowbat_fill, lv_color_black(), 0);
		lv_obj_set_style_bg_opa(lowbat_fill, LV_OPA_COVER, 0);

		/* Percentage in DSEG7 (digits only), centred inside the battery. */
		lowbat_pct_lbl = make_label(frame, &dseg7_18, 0);
		lv_obj_align(lowbat_pct_lbl, LV_ALIGN_CENTER, 0, 0);
		lv_label_set_text(lowbat_pct_lbl, "");

		lv_obj_t *title = make_label(lowbat_root, &b612_28, CONTENT_W);
		lv_label_set_text(title, "LOW BATTERY");
		lv_obj_align(title, LV_ALIGN_CENTER, 0, 40);

		lv_obj_t *hint = make_label(lowbat_root, &b612_16, CONTENT_W);
		lv_label_set_text(hint, "Please recharge");
		lv_obj_align(hint, LV_ALIGN_CENTER, 0, 72);
	}

	void build_reset(lv_obj_t *scr)
	{
		reset_root = make_view(scr);

		lv_obj_t *title = make_label(reset_root, &b612_28, CONTENT_W);
		lv_label_set_text(title, "FACTORY RESET");
		lv_obj_align(title, LV_ALIGN_CENTER, 0, -75);

		/* Big DSEG7 countdown (seconds left), matching the sensor values. */
		reset_seconds_lbl = make_label(reset_root, &dseg7_48, 0);
		lv_obj_align(reset_seconds_lbl, LV_ALIGN_CENTER, 0, 0);
		lv_label_set_text(reset_seconds_lbl, "");

		lv_obj_t *hint = make_label(reset_root, &b612_16, CONTENT_W);
		lv_label_set_text(hint, "Release to cancel");
		lv_obj_align(hint, LV_ALIGN_CENTER, 0, 72);
	}

} // namespace

int ui::init()
{
	if (!plat::display_ready())
	{
		plat::log("Display not ready\n");
		return -1;
	}

	lv_obj_t *scr = lv_scr_act();
	lv_obj_set_style_bg_color(scr, lv_color_white(), 0);
	lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

	build_status_bar(scr);
	build_boot(scr);
	build_sensor(scr);
	build_error(scr);
	build_lowbat(scr);
	build_reset(scr);
	ready = true;

	/* Boot splash: pending_view is VIEW_BOOT and shown is VIEW_NONE, so refresh()
	 * paints it with a full refresh. */
	ui::refresh();
	return 0;
}

void ui::set_sensor(uint16_t co2_ppm, int32_t temp_x100, uint16_t hum_x100)
{
	if (!ready)
	{
		return;
	}
	pending_view = VIEW_SENSOR;

	/* Dedup on the DISPLAYED value (CO2 exact ppm, T to 0.1 C, RH to whole %) so a
	 * change below what's shown (e.g. 26.03 -> 26.05 C, or 43.05 -> 43.17 % which
	 * both show "43") does not force an e-paper refresh. On the 30 s T+RH cadence a
	 * no-change tick then costs ~0.16 mAs (SCD41 read only) instead of a ~3 mAs
	 * refresh -- see docs/power-analysis.md. */
	const int32_t temp_q = ((temp_x100 + (temp_x100 >= 0 ? 5 : -5)) / 10) * 10;
	const uint16_t hum_q = (uint16_t)(((hum_x100 + 50) / 100) * 100);

	if (have_last_reading && co2_ppm == last_co2_ppm &&
		temp_q == last_temp_x100 && hum_q == last_hum_x100)
	{
		return; /* same displayed values already on the widgets */
	}

	char buf[24];
	snprintf(buf, sizeof(buf), "%u", co2_ppm);
	lv_label_set_text(co2_value, buf);
	snprintf(buf, sizeof(buf), "%u", (unsigned)(hum_q / 100));
	lv_label_set_text(hum_value, buf);
	const int whole = temp_q / 100;
	const int frac = abs(temp_q % 100) / 10;
	snprintf(buf, sizeof(buf), "%d.%d", whole, frac);
	lv_label_set_text(temp_value, buf);

	last_co2_ppm = co2_ppm;
	last_temp_x100 = temp_q;
	last_hum_x100 = hum_q;
	have_last_reading = true;
	dirty = true;
}

void ui::set_error(const char *title, const char *detail)
{
	if (!ready)
	{
		return;
	}
	pending_view = VIEW_ERROR;
	if (title)
	{
		lv_label_set_text(err_title_lbl, title);
	}
	lv_label_set_text(err_detail_lbl, detail ? detail : "");
	dirty = true;
}

void ui::set_low_battery()
{
	if (!ready)
	{
		return;
	}
	pending_view = VIEW_LOWBAT;

	/* Draws the level set_battery() last stored (already clamped). Before the first
	 * one it is -1, and the label simply stays empty. */
	if (last_batt_pct == last_lowbat_pct)
	{
		return; /* same value already on the widgets */
	}

	const int pct = (last_batt_pct < 0) ? 0 : last_batt_pct;

	/* Fill the ~122px interior of the big frame proportionally. */
	lv_obj_set_width(lowbat_fill, (lv_coord_t)(pct * 122 / 100));
	char buf[8];
	snprintf(buf, sizeof(buf), "%d", pct);
	lv_label_set_text(lowbat_pct_lbl, buf);

	last_lowbat_pct = last_batt_pct;
	dirty = true;
}

void ui::set_reset(uint8_t seconds_left)
{
	if (!ready)
	{
		return;
	}
	pending_view = VIEW_RESET;

	if ((int)seconds_left == last_reset_seconds)
	{
		return; /* same value already on the widgets */
	}

	char buf[8];
	snprintf(buf, sizeof(buf), "%u", seconds_left);
	lv_label_set_text(reset_seconds_lbl, buf);

	last_reset_seconds = seconds_left;
	dirty = true;
}

void ui::set_battery(uint8_t pct, bool charging)
{
	if (!ready)
	{
		return;
	}
	if (pct > 100)
	{
		pct = 100;
	}
	if ((int)pct == last_batt_pct && (int)charging == last_charging)
	{
		return;
	}

	lv_obj_set_width(batt_fill, (lv_coord_t)(pct * 20 / 100));

	char buf[8]; /* DSEG7 digits, no '%' (the font has no percent glyph). */
	snprintf(buf, sizeof(buf), "%u", pct);
	lv_label_set_text(batt_pct_lbl, buf);

	/* Charging: show the bolt in place of the percentage number. */
	if (charging)
	{
		lv_obj_add_flag(batt_pct_lbl, LV_OBJ_FLAG_HIDDEN);
		lv_obj_clear_flag(batt_bolt, LV_OBJ_FLAG_HIDDEN);
	}
	else
	{
		lv_obj_clear_flag(batt_pct_lbl, LV_OBJ_FLAG_HIDDEN);
		lv_obj_add_flag(batt_bolt, LV_OBJ_FLAG_HIDDEN);
	}

	last_batt_pct = pct;
	last_charging = charging;
	dirty = true;
}

void ui::set_link(Link state)
{
	if (!ready)
	{
		return;
	}
	if ((int)state == last_link)
	{
		return;
	}
	lv_label_set_text(link_lbl, link_token(state));
	last_link = (int)state;
	dirty = true;
}

void ui::refresh()
{
	if (!ready)
	{
		return;
	}
	const bool view_changed = (pending_view != shown_view);
	if (!dirty && !view_changed)
	{
		return; /* nothing changed since the last refresh */
	}

	if (view_changed)
	{
		hide_all_content();
		lv_obj_clear_flag(root_for(pending_view), LV_OBJ_FLAG_HIDDEN);
	}

	/* Full on a view change and periodically to clear ghosting; partial for
	 * in-place value / status-bar updates. */
	const bool full = view_changed || partials_since_full >= FULL_REFRESH_EVERY;
	flush(full);

	partials_since_full = full ? 0 : partials_since_full + 1;
	shown_view = pending_view;
	dirty = false;
}
