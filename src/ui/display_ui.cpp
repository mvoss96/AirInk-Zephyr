#include "display_ui.hpp"
#include "quantize.hpp"
#include "ui_platform.hpp"
#include "../version.hpp"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <lvgl.h>

/* The one place that reaches into LVGL's Zephyr heap shim. Only the target build can
 * weigh the pool: the host sim runs LVGL on a 16 MB malloc heap, with 64-bit pointers
 * and 8-bit colour, so any figure it produced would be a plausible-sounding lie. */
#ifdef CONFIG_SYS_HEAP_RUNTIME_STATS
#include <zephyr/sys/mem_stats.h>
#include <lvgl_mem.h>
#endif

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
	// The entries are centred as a block, so adding one keeps the menu balanced.
	constexpr int MENU_ITEM_H = 48;
	/* The entries are centred as a block, so a build with fewer of them stays balanced. */
	inline int menu_top(int rows) { return (CONTENT_H - rows * MENU_ITEM_H) / 2; }

	constexpr int BAR_W = 300, BAR_H = 32, BAR_BORDER = 3;
	constexpr int BAR_INNER_W = BAR_W - 2 * BAR_BORDER;

	// Status bar (never hidden).
	lv_obj_t *status_bar;
	lv_obj_t *batt_frame, *batt_fill, *batt_bolt, *batt_pct_lbl, *link_lbl;
	int last_charging = -1;

	/* Charging bolt as a filled 1bpp image (I1: index0 = black bolt, index1 =
	 * white bg, which is invisible on the white status bar). Shown in place of
	 * the percentage while charging. 12x16, generated from ASCII art. */
	const uint8_t bolt_map[] = {
		0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, // I1 palette: black, white
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

	// Content views (exactly one un-hidden at a time).
	lv_obj_t *boot_root;
	lv_obj_t *sensor_root, *co2_value, *hum_value, *temp_value;
	lv_obj_t *error_root, *err_title_lbl, *err_detail_lbl;
	lv_obj_t *lowbat_root, *lowbat_fill, *lowbat_pct_lbl;
	lv_obj_t *reset_root, *reset_seconds_lbl;
	lv_obj_t *menu_root, *menu_cursor;
	lv_obj_t *menu_item[(int)ui::Menu::Count];

	/* Which menu entries this build has, and where each one sits. A build with no radio has
	 * nothing to pair over, so it gets no Pairing entry -- and the entries below it move up,
	 * which is why the row is looked up rather than computed from the enum. -1 = absent. */
	int menu_row[(int)ui::Menu::Count];
	int menu_rows;

	/* The pairing view. The QR canvas is an I1 (1-bit indexed) draw buffer that lv_qrcode
	 * allocates from the LVGL pool when we hand it the payload -- so a build without codes
	 * never pays for it. */
	lv_obj_t *pair_root, *pair_qr_obj, *pair_code_lbl;

	/* The two calibration steps share one set of widgets. They still get one View each,
	 * so the step change is a view change and thus a full refresh -- the big DSEG7
	 * digits appearing would ghost badly under a partial one. Within the countdown the
	 * view does not change, so its ticks stay partial. */
	lv_obj_t *calib_root, *calib_title, *calib_bar, *calib_bar_fill;
	lv_obj_t *calib_body, *calib_hint;

	/* Views + refresh bookkeeping. Setters stage `pending_view` and dirty the
	 * widgets; ui::refresh() commits: hide/show the view + ONE panel refresh. */
	enum View
	{
		VIEW_NONE,
		VIEW_BOOT,
		VIEW_SENSOR,
		VIEW_ERROR,
		VIEW_LOWBAT,
		VIEW_RESET,
		VIEW_MENU,
		VIEW_PAIRING,
		VIEW_CALIB_PROMPT,
		VIEW_CALIB_PROGRESS
	};
	View shown_view = VIEW_NONE;   // what the panel currently shows
	View pending_view = VIEW_BOOT; // staged by a set_<view>; committed by refresh()
	bool dirty;                    // a setter changed something since the last refresh
	int partials_since_full;
	bool ready; // init() built the widgets; every entry point no-ops until then

	/* Skip-refresh dedup. The last_* sentinels are int, not the API's uint8_t,
	 * because they use -1 for "nothing shown yet". */
	bool have_last_reading;
	uint16_t last_co2_ppm, last_hum_x100;
	int32_t last_temp_x100;
	int last_batt_pct = -1;
	int last_link = -1;
	int last_lowbat_pct = -1;
	int last_reset_seconds = -1;
	int last_menu_sel = -1;
	int last_calib_pct = -1;

	// ---- pool accounting ----

	/** Bytes currently allocated from the LVGL pool.
	 *
	 * @return the figure, or 0 where it cannot be measured (the host sim)
	 */
	uint32_t heap_used()
	{
#ifdef CONFIG_SYS_HEAP_RUNTIME_STATS
		struct sys_memory_stats s{};
		lvgl_heap_stats(&s);
		return (uint32_t)s.allocated_bytes;
#else
		return 0;
#endif
	}

	/** Log what one builder cost the pool. Views are resident, so this is what it
	 * costs forever, not just during init().
	 *
	 * @param view   name for the log line
	 * @param before heap_used() from just before the builder ran
	 * @return heap_used() now, ready to be passed to the next call
	 */
	uint32_t log_built(const char *view, uint32_t before)
	{
		const uint32_t now = heap_used();
#ifdef CONFIG_SYS_HEAP_RUNTIME_STATS
		char line[48];
		snprintf(line, sizeof(line), "[LVGL] %-11s %5u B\n", view, (unsigned)(now - before));
		plat::log(line);
#else
		(void)view;
		(void)before;
#endif
		return now;
	}

	// ---- widget helpers ----

	/** Create a centred black label.
	 *
	 * @param parent widget to attach to
	 * @param font   one of the B612 / DSEG7 faces
	 * @param w      fixed width, or 0 to size to the text
	 * @return the new label
	 */
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

	/** Create a solid black rectangle, used as a rule or a battery nub.
	 *
	 * @param parent widget to attach to
	 * @param w      width in pixels
	 * @param h      height in pixels
	 * @return the new rectangle, unpositioned
	 */
	lv_obj_t *make_divider(lv_obj_t *parent, lv_coord_t w, lv_coord_t h)
	{
		lv_obj_t *d = lv_obj_create(parent);
		lv_obj_remove_style_all(d);
		lv_obj_set_size(d, w, h);
		lv_obj_set_style_bg_color(d, lv_color_black(), 0);
		lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
		return d;
	}

	/** Create a full-width white content container below the status bar.
	 *
	 * @param parent the active screen
	 * @return the new container, positioned at y=CONTENT_Y
	 */
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

	/** Push the current LVGL frame to the panel.
	 * The ssd16xx driver does a partial refresh whenever blanking is off; wrapping the
	 * flush in blanking on/off forces a full refresh instead.
	 *
	 * @param full true for a full refresh (clean, but a black flash), false for a
	 *             partial one (fast, only changed pixels, no flash)
	 */
	void flush(bool full)
	{
		lv_display_t *disp = lv_display_get_default();

		// Wake the panel for the whole refresh; the full-refresh update fires in
		// blanking_off(), so suspend only after that.
		plat::display_resume();
		if (full)
		{
			plat::blanking_on();  // select the full-refresh profile
			lv_refr_now(disp);	  // write RAM (no refresh yet)
			plat::blanking_off(); // trigger the full refresh
		}
		else
		{
			lv_refr_now(disp); // partial refresh
		}
		plat::display_suspend(); // deep-sleep the panel until the next refresh
	}

	/** Hide every content view; the status bar stays visible. */
	void hide_all_content()
	{
		lv_obj_add_flag(boot_root, LV_OBJ_FLAG_HIDDEN);
		lv_obj_add_flag(sensor_root, LV_OBJ_FLAG_HIDDEN);
		lv_obj_add_flag(error_root, LV_OBJ_FLAG_HIDDEN);
		lv_obj_add_flag(lowbat_root, LV_OBJ_FLAG_HIDDEN);
		lv_obj_add_flag(reset_root, LV_OBJ_FLAG_HIDDEN);
		lv_obj_add_flag(menu_root, LV_OBJ_FLAG_HIDDEN);
		lv_obj_add_flag(calib_root, LV_OBJ_FLAG_HIDDEN);
	}

	/** Is this a view the user steps through with the button?
	 *
	 * A full refresh is a ~2 s black flash. Paying it on every cursor move makes the
	 * device feel broken, so these views are entered and navigated with partial
	 * refreshes. The ghosting they leave -- the inverted cursor bar and the big DSEG7
	 * digits are the worst of it -- is cleared by the one full refresh that happens on
	 * the way back out to a resting view.
	 *
	 * @param v the view to classify
	 * @return true for the menu, the calibration steps and the reset countdown
	 */
	bool transient(View v)
	{
		return v == VIEW_MENU || v == VIEW_PAIRING || v == VIEW_CALIB_PROMPT ||
			   v == VIEW_CALIB_PROGRESS || v == VIEW_RESET;
	}

	/** The container that belongs to a view.
	 *
	 * @param v the view to look up
	 * @return its root container; the boot splash for VIEW_NONE
	 */
	lv_obj_t *root_for(View v)
	{
		switch (v)
		{
		case VIEW_SENSOR: return sensor_root;
		case VIEW_ERROR:  return error_root;
		case VIEW_LOWBAT: return lowbat_root;
		case VIEW_RESET:  return reset_root;
		case VIEW_MENU:   return menu_root;
		case VIEW_PAIRING: return pair_root;
		case VIEW_CALIB_PROMPT:
		case VIEW_CALIB_PROGRESS: return calib_root;
		default:          return boot_root;
		}
	}

	/** The short text shown in the status bar for a radio state.
	 *
	 * @param state the connectivity state
	 * @return a static string, e.g. "BLE" or "ZB.."; "--" when there is no link
	 */
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
		case ui::Link::ThreadJoining:
			return "TH..";
		case ui::Link::ThreadConnected:
			return "TH";
		case ui::Link::None:
			break;
		}
		return "--";
	}

	// ---- builders (once, in init); `scr` is the active screen ----

	/** Build the always-visible status bar: battery, bolt, link token. */
	void build_status_bar(lv_obj_t *scr)
	{
		status_bar = lv_obj_create(scr);
		lv_obj_remove_style_all(status_bar);
		lv_obj_set_size(status_bar, SCR_W, STATUS_H);
		lv_obj_set_pos(status_bar, 0, 0);
		lv_obj_set_style_bg_color(status_bar, lv_color_white(), 0);
		lv_obj_set_style_bg_opa(status_bar, LV_OPA_COVER, 0);
		lv_obj_clear_flag(status_bar, LV_OBJ_FLAG_SCROLLABLE);

		// Battery: small outline frame + proportional fill + a nub on the right.
		// No percentage text — DSEG7 has no '%' glyph and only exists at 48px, so
		// the fill level is the sole (glanceable) indicator.
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
		lv_obj_set_size(batt_fill, 0, 9); // width set in set_battery()
		lv_obj_align(batt_fill, LV_ALIGN_LEFT_MID, 1, 0);
		lv_obj_set_style_bg_color(batt_fill, lv_color_black(), 0);
		lv_obj_set_style_bg_opa(batt_fill, LV_OPA_COVER, 0);

		// Percentage in DSEG7 (digits only, no '%') to match the sensor values.
		batt_pct_lbl = make_label(status_bar, &dseg7_18, 0);
		lv_obj_align_to(batt_pct_lbl, batt_frame, LV_ALIGN_OUT_RIGHT_MID, 10, 0);
		lv_label_set_text(batt_pct_lbl, "");

		// Charging bolt, shown in place of the percentage while USB is present
		// (see set_battery()). Same anchor as the number.
		batt_bolt = lv_image_create(status_bar);
		lv_image_set_src(batt_bolt, &bolt_img);
		lv_obj_align_to(batt_bolt, batt_frame, LV_ALIGN_OUT_RIGHT_MID, 10, 0);
		lv_obj_add_flag(batt_bolt, LV_OBJ_FLAG_HIDDEN);

		// Link: short text token, right-aligned.
		link_lbl = make_label(status_bar, &b612_14, 0);
		lv_obj_align(link_lbl, LV_ALIGN_RIGHT_MID, -6, 0);
		lv_label_set_text(link_lbl, link_token(ui::Link::None));

		// Divider under the bar.
		lv_obj_t *sep = make_divider(status_bar, SCR_W, 1);
		lv_obj_align(sep, LV_ALIGN_BOTTOM_MID, 0, 0);
	}

	/** Build the boot splash: wordmark, tagline, author, build stamp. */
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

		// Firmware version + build date/time (compile-time).
		lv_obj_t *build = make_label(boot_root, &b612_14, CONTENT_W);
		lv_label_set_text(build, "v" AIRINK_VERSION "  " __DATE__ "  " __TIME__);
		lv_obj_align(build, LV_ALIGN_BOTTOM_MID, 0, -6);
	}

	/** Build the sensor view: CO2 on top, humidity and temperature below. */
	void build_sensor(lv_obj_t *scr)
	{
		sensor_root = make_view(scr);

		// Each metric is a cell: big value + small unit on one baseline, caption
		// pinned below. Returns the value label so callers can update it.
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
			// FLEX_ALIGN_END aligns box bottoms, not text baselines; nudge the small
			// unit up by the descent difference so the baselines line up.
			lv_obj_set_style_translate_y(u, (lv_coord_t)(b612_28.base_line - dseg7_48.base_line), 0);
			lv_obj_align(row, LV_ALIGN_CENTER, 0, 0);

			lv_obj_t *nm = make_label(cell, &b612_16, w);
			lv_label_set_text(nm, name);
			lv_obj_align(nm, LV_ALIGN_BOTTOM_MID, 0, -6);
			return val;
		};

		constexpr int SPLIT_Y = CONTENT_H / 2; // 136

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

	/** Build the error view: a title line and a detail line. */
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

	/** Build the low-battery warning: a large battery glyph and a hint. */
	void build_lowbat(lv_obj_t *scr)
	{
		lowbat_root = make_view(scr);

		// Big battery outline + a small proportional fill + nub.
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
		lv_obj_set_size(lowbat_fill, 0, 44); // width set in show_low_battery()
		lv_obj_align(lowbat_fill, LV_ALIGN_LEFT_MID, 4, 0);
		lv_obj_set_style_bg_color(lowbat_fill, lv_color_black(), 0);
		lv_obj_set_style_bg_opa(lowbat_fill, LV_OPA_COVER, 0);

		// Percentage in DSEG7 (digits only), centred inside the battery.
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

	/** Build the factory-reset view: a large countdown and a hint. */
	void build_reset(lv_obj_t *scr)
	{
		reset_root = make_view(scr);

		lv_obj_t *title = make_label(reset_root, &b612_28, CONTENT_W);
		lv_label_set_text(title, "FACTORY RESET");
		lv_obj_align(title, LV_ALIGN_CENTER, 0, -75);

		// Big DSEG7 countdown (seconds left), matching the sensor values.
		reset_seconds_lbl = make_label(reset_root, &dseg7_48, 0);
		lv_obj_align(reset_seconds_lbl, LV_ALIGN_CENTER, 0, 0);
		lv_label_set_text(reset_seconds_lbl, "");

		lv_obj_t *hint = make_label(reset_root, &b612_16, CONTENT_W);
		lv_label_set_text(hint, "Release to cancel");
		lv_obj_align(hint, LV_ALIGN_CENTER, 0, 72);
	}

	/** Build the settings menu: a header, one label per entry, a moving cursor.
	 *
	 * Only the entries this build has: Pairing is skipped when there are no onboarding codes,
	 * and the entries below it move up. menu_row[] records where each one landed, so the
	 * cursor and the highlight bar agree with what is drawn.
	 *
	 * @param scr           the active screen
	 * @param with_pairing  whether the Pairing entry exists
	 */
	void build_menu(lv_obj_t *scr, bool with_pairing)
	{
		menu_root = make_view(scr);

		lv_obj_t *hdr = make_label(menu_root, &b612_16, CONTENT_W);
		lv_label_set_text(hdr, "MENU");
		lv_obj_align(hdr, LV_ALIGN_TOP_MID, 0, 8);

		lv_obj_t *rule = make_divider(menu_root, CONTENT_W - 48, 1);
		lv_obj_align(rule, LV_ALIGN_TOP_MID, 0, 32);

		static const char *const names[] = {"Calibrate CO2", "Pairing code", "Exit"};
		static_assert(sizeof(names) / sizeof(names[0]) == (size_t)ui::Menu::Count,
					  "every menu entry needs a label");

		menu_rows = 0;
		for (int i = 0; i < (int)ui::Menu::Count; i++)
		{
			const bool present = with_pairing || i != (int)ui::Menu::Pairing;
			menu_row[i] = present ? menu_rows++ : -1;
		}

		// The cursor is created before the labels so they draw on top of it: a
		// selected entry is white text on the black bar, which is the only way to
		// mark a selection without a glyph B612's ASCII subset does not have.
		menu_cursor = make_divider(menu_root, CONTENT_W - 32, MENU_ITEM_H);
		lv_obj_set_pos(menu_cursor, 16, menu_top(menu_rows));

		for (int i = 0; i < (int)ui::Menu::Count; i++)
		{
			if (menu_row[i] < 0)
			{
				menu_item[i] = nullptr;
				continue;
			}
			menu_item[i] = make_label(menu_root, &b612_28, CONTENT_W - 32);
			lv_label_set_text(menu_item[i], names[i]);
			lv_obj_set_pos(menu_item[i], 16, menu_top(menu_rows) + menu_row[i] * MENU_ITEM_H + 7);
		}

		lv_obj_t *hint = make_label(menu_root, &b612_14, CONTENT_W);
		lv_label_set_text(hint, "Tap = next     Hold = select");
		lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -8);
	}

	/** Build the pairing view: the onboarding QR, with the manual code under it.
	 *
	 * The QR is the whole point -- a camera reads it in a second -- but it is also the thing
	 * most likely to fail: a dirty lens, a bad angle, a controller that insists on typing. So
	 * the human-readable code sits below it, always, not behind a second screen.
	 *
	 * lv_qrcode renders into an I1 (1-bit indexed) draw buffer, which is exactly this panel's
	 * format: no dithering, no anti-aliasing, every module a whole pixel block. The buffer is
	 * allocated from the LVGL pool the moment we hand it the payload, and it is sized by the
	 * payload -- so this view is not free, and ui::log_pool() at boot is where you see what it
	 * cost.
	 *
	 * @param scr    the active screen
	 * @param qr     the onboarding payload ("MT:...")
	 * @param manual the same code for humans, drawn below the QR
	 */
	void build_pairing(lv_obj_t *scr, const char *qr, const char *manual)
	{
		pair_root = make_view(scr);

		lv_obj_t *hdr = make_label(pair_root, &b612_16, CONTENT_W);
		lv_label_set_text(hdr, "PAIRING CODE");
		lv_obj_align(hdr, LV_ALIGN_TOP_MID, 0, 6);

		/* Sized so the module grid lands on whole pixels: the Matter payload is ~22 chars,
		 * which QR encodes at 29x29 modules, and 174 = 6 px per module. A quiet zone is not
		 * drawn -- the panel around it is white, which is the same thing. */
		pair_qr_obj = lv_qrcode_create(pair_root);
		lv_qrcode_set_size(pair_qr_obj, 174);
		lv_qrcode_set_dark_color(pair_qr_obj, lv_color_black());
		lv_qrcode_set_light_color(pair_qr_obj, lv_color_white());
		lv_qrcode_update(pair_qr_obj, qr, strlen(qr));
		lv_obj_align(pair_qr_obj, LV_ALIGN_TOP_MID, 0, 32);

		pair_code_lbl = make_label(pair_root, &b612_28, CONTENT_W);
		lv_label_set_text(pair_code_lbl, manual);
		lv_obj_align(pair_code_lbl, LV_ALIGN_BOTTOM_MID, 0, -26);

		lv_obj_t *hint = make_label(pair_root, &b612_14, CONTENT_W);
		lv_label_set_text(hint, "Tap to go back");
		lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -6);
	}

	/** Build the shared calibration skeleton: title, progress bar, body, hint. */
	void build_calib(lv_obj_t *scr)
	{
		calib_root = make_view(scr);

		calib_title = make_label(calib_root, &b612_28, CONTENT_W);
		lv_obj_align(calib_title, LV_ALIGN_TOP_MID, 0, 16);

		// Outline + proportional fill, the same primitives as the battery gauge: no
		// LVGL bar widget, no anti-aliasing to smear on a 1-bit panel.
		calib_bar = lv_obj_create(calib_root);
		lv_obj_remove_style_all(calib_bar);
		lv_obj_set_size(calib_bar, BAR_W, BAR_H);
		lv_obj_align(calib_bar, LV_ALIGN_CENTER, 0, -10);
		lv_obj_set_style_border_color(calib_bar, lv_color_black(), 0);
		lv_obj_set_style_border_width(calib_bar, BAR_BORDER, 0);
		lv_obj_clear_flag(calib_bar, LV_OBJ_FLAG_SCROLLABLE);

		calib_bar_fill = lv_obj_create(calib_bar);
		lv_obj_remove_style_all(calib_bar_fill);
		lv_obj_set_size(calib_bar_fill, 0, BAR_H - 4 * BAR_BORDER); // width set per update
		lv_obj_align(calib_bar_fill, LV_ALIGN_LEFT_MID, BAR_BORDER, 0);
		lv_obj_set_style_bg_color(calib_bar_fill, lv_color_black(), 0);
		lv_obj_set_style_bg_opa(calib_bar_fill, LV_OPA_COVER, 0);

		calib_body = make_label(calib_root, &b612_16, CONTENT_W - 60);

		calib_hint = make_label(calib_root, &b612_14, CONTENT_W);
		lv_obj_align(calib_hint, LV_ALIGN_BOTTOM_MID, 0, -8);
	}

	/** Fill the calibration skeleton for one step.
	 *
	 * @param title   headline
	 * @param show_bar false hides the progress bar and centres the body in its place
	 * @param body    explanation
	 * @param hint    what the button does here
	 */
	void calib_fill(const char *title, bool show_bar, const char *body, const char *hint)
	{
		lv_label_set_text(calib_title, title);
		lv_label_set_text(calib_hint, hint);
		lv_label_set_text(calib_body, body);

		if (show_bar)
		{
			lv_obj_clear_flag(calib_bar, LV_OBJ_FLAG_HIDDEN);
		}
		else
		{
			lv_obj_add_flag(calib_bar, LV_OBJ_FLAG_HIDDEN);
		}
		lv_obj_align(calib_body, LV_ALIGN_CENTER, 0, show_bar ? 52 : -10);
	}

} // namespace

int ui::init(const char *pair_qr, const char *pair_manual)
{
	if (!plat::display_ready())
	{
		plat::log("Display not ready\n");
		return -1;
	}

	lv_obj_t *scr = lv_scr_act();
	lv_obj_set_style_bg_color(scr, lv_color_white(), 0);
	lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

	// Weigh each view as it is built: they are all resident, so this is the standing
	// cost of the pool, and the number to look at before adding the next screen.
	uint32_t h = heap_used();
	build_status_bar(scr);
	h = log_built("status bar", h);
	build_boot(scr);
	h = log_built("boot", h);
	build_sensor(scr);
	h = log_built("sensor", h);
	build_error(scr);
	h = log_built("error", h);
	build_lowbat(scr);
	h = log_built("lowbat", h);
	build_reset(scr);
	h = log_built("reset", h);
	build_menu(scr, pair_qr != nullptr);
	h = log_built("menu", h);
	build_calib(scr);
	h = log_built("calib", h);
	/* Only when there is something to pair over -- otherwise no view, and the QR's draw buffer
	 * (the largest single allocation in the pool) is never made. */
	if (pair_qr)
	{
		build_pairing(scr, pair_qr, pair_manual ? pair_manual : "");
		log_built("pairing", h);
	}
	ready = true;

	// Boot splash: pending_view is VIEW_BOOT and shown is VIEW_NONE, so refresh()
	// paints it with a full refresh.
	ui::refresh();
	return 0;
}

bool ui::menu_has(Menu entry)
{
	return ready && menu_row[(int)entry] >= 0;
}

void ui::show_pairing()
{
	if (!ready || !pair_root)
	{
		return;
	}
	pending_view = VIEW_PAIRING;
	dirty = true;
}

void ui::show_sensor()
{
	if (!ready)
	{
		return;
	}
	pending_view = VIEW_SENSOR; // the widgets keep whatever set_sensor() last wrote
}

void ui::set_sensor(uint16_t co2_ppm, int32_t temp_x100, uint16_t hum_x100)
{
	if (!ready)
	{
		return;
	}

	// Dedup on the DISPLAYED value (CO2 exact ppm, T to 0.1 C, RH to whole %) so a
	// change below what's shown (e.g. 26.03 -> 26.05 C, or 43.05 -> 43.17 % which
	// both show "43") does not force an e-paper refresh. On the 30 s T+RH cadence a
	// no-change tick then costs ~0.16 mAs (SCD41 read only) instead of a ~3 mAs
	// refresh -- see docs/power-analysis.md.
	const int32_t temp_q = quantize_temp_x100(temp_x100);
	const uint16_t hum_q = quantize_hum_x100(hum_x100);

	if (have_last_reading && co2_ppm == last_co2_ppm &&
		temp_q == last_temp_x100 && hum_q == last_hum_x100)
	{
		return; // same displayed values already on the widgets
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

	// Draws the level set_battery() last stored (already clamped). Before the first
	// one it is -1, and the label simply stays empty.
	if (last_batt_pct == last_lowbat_pct)
	{
		return; // same value already on the widgets
	}

	const int pct = (last_batt_pct < 0) ? 0 : last_batt_pct;

	// Fill the ~122px interior of the big frame proportionally.
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
		return; // same value already on the widgets
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

	char buf[8]; // DSEG7 digits, no '%' (the font has no percent glyph).
	snprintf(buf, sizeof(buf), "%u", pct);
	lv_label_set_text(batt_pct_lbl, buf);

	// Charging: show the bolt in place of the percentage number.
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

void ui::set_menu(Menu selected)
{
	if (!ready)
	{
		return;
	}
	pending_view = VIEW_MENU;

	const int sel = (int)selected;
	if (sel == last_menu_sel)
	{
		return; // same entry already highlighted
	}

	if (menu_row[sel] < 0)
	{
		return; // an entry this build does not have; menu.cpp must not select it
	}

	for (int i = 0; i < (int)Menu::Count; i++)
	{
		if (menu_item[i])
		{
			lv_obj_set_style_text_color(menu_item[i],
										(i == sel) ? lv_color_white() : lv_color_black(), 0);
		}
	}
	lv_obj_set_y(menu_cursor, menu_top(menu_rows) + menu_row[sel] * MENU_ITEM_H);

	last_menu_sel = sel;
	dirty = true;
}

void ui::set_calib_prompt()
{
	if (!ready)
	{
		return;
	}
	pending_view = VIEW_CALIB_PROMPT;
	calib_fill("CALIBRATE CO2", false,
			   "Take the device outside, or hold it at a wide open window, "
			   "and leave it there for three minutes.",
			   "Hold = start     Tap = cancel");
	last_calib_pct = -1; // the bar has not run yet
	dirty = true;
}

void ui::set_calib_progress(uint8_t pct)
{
	if (!ready)
	{
		return;
	}
	pending_view = VIEW_CALIB_PROGRESS;

	if (pct > 100)
	{
		pct = 100;
	}
	if ((int)pct == last_calib_pct)
	{
		return; // the bar is already at this length
	}

	// A full bar means the three minutes are over and the corrected value is being read,
	// which takes ~5 s more. There is nothing left to abort, so the hint stops offering
	// it: a hold here reaches the sensor view and opens the menu, like a hold anywhere.
	const bool warming_up = (pct < 100);
	calib_fill("CALIBRATING", true,
			   warming_up ? "Leave the device in the fresh air." : "Reading the corrected value.",
			   warming_up ? "Hold to abort" : "");
	lv_obj_set_width(calib_bar_fill, (lv_coord_t)(pct * BAR_INNER_W / 100));

	last_calib_pct = pct;
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
		return; // nothing changed since the last refresh
	}

	if (view_changed)
	{
		hide_all_content();
		lv_obj_clear_flag(root_for(pending_view), LV_OBJ_FLAG_HIDDEN);
	}

	// Full on the first paint, on arriving at a resting view (which also cleans up
	// after the transient ones), and periodically to clear ghosting. Everything the
	// user navigates through, and every in-place value update, is partial.
	const bool full = (shown_view == VIEW_NONE) ||
					  partials_since_full >= FULL_REFRESH_EVERY ||
					  (view_changed && !transient(pending_view));
	flush(full);

	partials_since_full = full ? 0 : partials_since_full + 1;
	shown_view = pending_view;
	dirty = false;
}

void ui::log_pool(const char *tag)
{
#ifdef CONFIG_SYS_HEAP_RUNTIME_STATS
	struct sys_memory_stats s{};
	lvgl_heap_stats(&s);

	char line[80];
	snprintf(line, sizeof(line), "[LVGL] %-12s used %u  peak %u  free %u  of %u B\n", tag,
			 (unsigned)s.allocated_bytes, (unsigned)s.max_allocated_bytes,
			 (unsigned)s.free_bytes, (unsigned)CONFIG_LV_Z_MEM_POOL_SIZE);
	plat::log(line);
#else
	(void)tag;
#endif
}
