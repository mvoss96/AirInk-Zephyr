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
	/* The menu is a block of entries and a hint line under them; the entries get what is left.
	 *
	 * There used to be a "MENU" title and a rule above the list. They went, and taking them out is
	 * what makes five entries fit comfortably: a list with a cursor on it, over a line that reads
	 * "Tap = next   Hold = select", does not also need to be told it is a menu. The 36 px they cost
	 * are worth more as space between the rows.
	 *
	 * The height matters more than it looks. This block used to be centred in the whole content
	 * area, which held up exactly until a fifth entry arrived: the block grew, its top crossed the
	 * header rule, and "Calibrate CO2" was painted straight through it. Nothing complained -- LVGL
	 * will happily draw a label over a line. Hence the static_assert: the next entry that does not
	 * fit fails the build instead of the panel. */
	constexpr int MENU_HINT_H = 26; // "Tap = next   Hold = select"
	constexpr int MENU_ITEM_H = 44;

	static_assert(ui::LIST_MAX_ROWS * MENU_ITEM_H + MENU_HINT_H <= CONTENT_H,
				  "the menu entries no longer fit above the hint -- shrink MENU_ITEM_H, or the "
				  "panel will draw them on top of each other");

	inline int menu_top(int rows)
	{
		return (CONTENT_H - MENU_HINT_H - rows * MENU_ITEM_H) / 2;
	}

	constexpr int BAR_W = 300, BAR_H = 32, BAR_BORDER = 3;
	constexpr int BAR_INNER_W = BAR_W - 2 * BAR_BORDER;

	// Status bar (never hidden).
	lv_obj_t *status_bar;
	lv_obj_t *batt_frame, *batt_fill, *batt_bolt, *batt_pct_lbl;
	int last_charging = -1;

	/* The signal bars: four ascending, filled = earned, outline = not. The whole radio vocabulary of
	 * the panel -- see ui::set_signal_bars(). */
	constexpr int SIGNAL_BARS = 4;
	lv_obj_t *signal_bar[SIGNAL_BARS];
	lv_obj_t *link_icon; // the Matter mark; it shares the bars' fate
	int last_bars = -1;  // what set_signal_bars() last drew; -1 = the corner is empty

	/* Whether this build has a radio at all. Without one the right-hand side of the status bar stays
	 * empty -- no bars, and no "--" either. A dash is an answer to "how is the link?", and a device
	 * that cannot have a link is not being asked. It used to sit there for the life of the standalone
	 * firmware, quietly implying that something was missing. */
	bool has_radio;

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
	/* The Matter mark, rasterised from the official SVG rather than drawn by eye, in the two sizes
	 * this UI has room for: 22 px beside the signal bars, 48 px on the boot splash.
	 *
	 * Which mark matters. The old Thread logo -- a circle with a thread and a needle's eye -- was
	 * tried first and does not survive: its strokes are hairlines, and a hairline inside a 22 px disc
	 * is one pixel. It rendered as a smudge. This one is three thick converging arrows and nothing
	 * else, so there is nothing to lose; it is legible from 20 px up. Shape decides, not care.
	 *
	 * I1 (1-bit indexed), same idiom as the charging bolt: index0 = black ink, index1 = white, which
	 * is invisible against the panel.
	 *
	 * NB: the mark is a trademark of the Connectivity Standards Alliance, licensed with
	 * certification. This is a private build; a product would need to earn it. */
	const uint8_t matter_map[] = {
		0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, // I1: black, white
		0xff, 0xff, 0xff,
		0xff, 0xcf, 0xff,
		0xff, 0xcf, 0xff,
		0xff, 0xcf, 0xff,
		0xff, 0xcf, 0xff,
		0xff, 0xcf, 0xff,
		0xfc, 0xcc, 0x7f,
		0xf8, 0x00, 0x7f,
		0xfc, 0x00, 0xff,
		0xff, 0x03, 0xff,
		0xe7, 0xff, 0x9f,
		0xe3, 0xff, 0x1f,
		0xe1, 0xfe, 0x1f,
		0xf8, 0xfc, 0x7f,
		0xf8, 0x78, 0x7f,
		0xf0, 0x78, 0x3f,
		0xc0, 0x38, 0x0f,
		0x86, 0x31, 0x83,
		0x8e, 0x31, 0xe3,
		0xfe, 0x31, 0xff,
		0xff, 0x33, 0xff,
		0xff, 0xff, 0xff,
	};
	const lv_image_dsc_t matter_img = {
		.header = {
			.magic = LV_IMAGE_HEADER_MAGIC,
			.cf = LV_COLOR_FORMAT_I1,
			.flags = 0,
			.w = 22,
			.h = 22,
			.stride = 3,
			.reserved_2 = 0,
		},
		.data_size = sizeof(matter_map),
		.data = matter_map,
		.reserved = NULL,
	};

	const uint8_t matter_big_map[] = {
		0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, // I1: black, white
		0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
		0xff, 0xff, 0xff, 0x7f, 0xff, 0xff,
		0xff, 0xff, 0xfc, 0x3f, 0xff, 0xff,
		0xff, 0xff, 0xfc, 0x1f, 0xff, 0xff,
		0xff, 0xff, 0xfc, 0x1f, 0xff, 0xff,
		0xff, 0xff, 0xfc, 0x1f, 0xff, 0xff,
		0xff, 0xff, 0xfc, 0x1f, 0xff, 0xff,
		0xff, 0xff, 0xfc, 0x1f, 0xff, 0xff,
		0xff, 0xff, 0xfc, 0x1f, 0xff, 0xff,
		0xff, 0xff, 0xfc, 0x1f, 0xff, 0xff,
		0xff, 0xff, 0xfc, 0x1f, 0xff, 0xff,
		0xff, 0xff, 0xfc, 0x1f, 0xff, 0xff,
		0xff, 0xff, 0xfc, 0x1f, 0xff, 0xff,
		0xff, 0xfc, 0xfc, 0x1f, 0x9f, 0xff,
		0xff, 0xf0, 0x3c, 0x1e, 0x07, 0xff,
		0xff, 0xc0, 0x08, 0x10, 0x03, 0xff,
		0xff, 0xe0, 0x00, 0x00, 0x07, 0xff,
		0xff, 0xf0, 0x00, 0x00, 0x0f, 0xff,
		0xff, 0xfc, 0x00, 0x00, 0x1f, 0xff,
		0xff, 0xff, 0x00, 0x00, 0x7f, 0xff,
		0xff, 0xff, 0xc0, 0x03, 0xff, 0xff,
		0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
		0xfe, 0x7f, 0xff, 0xff, 0xfe, 0x3f,
		0xfe, 0x1f, 0xff, 0xff, 0xf8, 0x3f,
		0xfe, 0x07, 0xff, 0xff, 0xf0, 0x3f,
		0xfe, 0x03, 0xff, 0xff, 0xc0, 0x3f,
		0xfe, 0x01, 0xff, 0xff, 0x80, 0x3f,
		0xfe, 0x00, 0xff, 0xff, 0x00, 0x7f,
		0xff, 0x80, 0x7f, 0xfe, 0x01, 0xff,
		0xff, 0xe0, 0x3f, 0xfe, 0x03, 0xff,
		0xff, 0xf0, 0x1f, 0xfc, 0x07, 0xff,
		0xff, 0xf0, 0x1f, 0xf8, 0x0f, 0xff,
		0xff, 0xe0, 0x0f, 0xf8, 0x03, 0xff,
		0xff, 0x80, 0x0f, 0xf0, 0x00, 0xff,
		0xfe, 0x00, 0x0f, 0xf0, 0x00, 0x7f,
		0xf8, 0x00, 0x07, 0xf0, 0x00, 0x1f,
		0xf0, 0x00, 0x07, 0xe0, 0x40, 0x07,
		0xc0, 0x07, 0x07, 0xe0, 0x70, 0x01,
		0x80, 0x1f, 0x07, 0xe0, 0x78, 0x01,
		0x80, 0x7f, 0x07, 0xe0, 0xfe, 0x01,
		0x80, 0xff, 0x07, 0xe0, 0xff, 0x81,
		0xc3, 0xff, 0x07, 0xe0, 0xff, 0xe3,
		0xff, 0xff, 0x07, 0xe0, 0xff, 0xff,
		0xff, 0xff, 0x07, 0xe0, 0xff, 0xff,
		0xff, 0xff, 0xc7, 0xe3, 0xff, 0xff,
		0xff, 0xff, 0xf7, 0xe7, 0xff, 0xff,
		0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
		0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	};
	const lv_image_dsc_t matter_big_img = {
		.header = {
			.magic = LV_IMAGE_HEADER_MAGIC,
			.cf = LV_COLOR_FORMAT_I1,
			.flags = 0,
			.w = 48,
			.h = 48,
			.stride = 6,
			.reserved_2 = 0,
		},
		.data_size = sizeof(matter_big_map),
		.data = matter_big_map,
		.reserved = NULL,
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
	lv_obj_t *sensor_root, *co2_value, *hum_value, *temp_value, *temp_unit_lbl;
	lv_obj_t *error_root, *err_title_lbl, *err_detail_lbl;
	lv_obj_t *lowbat_root, *lowbat_fill, *lowbat_pct_lbl;
	lv_obj_t *reset_root;
	/* THE menu. One set of widgets, and it draws whatever list it is handed -- it does not know that a
	 * root and a Calibrate sub-menu exist, because to a panel they are five strings and a cursor,
	 * twice. */
	lv_obj_t *list_root, *list_cursor;
	lv_obj_t *list_item[ui::LIST_MAX_ROWS];
	int list_rows = 0; // how many are in use right now; the rest are hidden
	int list_sel = -1; // -1 = nothing drawn yet

	/* The one-button number editor, behind any row that carries a number. */
	lv_obj_t *value_root, *value_title, *value_num, *value_unit, *value_sub, *value_hint;

	/* The pairing view. The QR canvas is an I1 (1-bit indexed) draw buffer that lv_qrcode
	 * allocates from the LVGL pool when we hand it the payload -- so a build without codes
	 * never pays for it. */
	lv_obj_t *pair_root, *pair_qr_obj, *pair_code_lbl, *pair_state_lbl;

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
		VIEW_VALUE,
		VIEW_PAIRING,
		VIEW_CALIB_PROMPT,
		VIEW_CALIB_PROGRESS,
		VIEW_COUNT
	};
	View shown_view = VIEW_NONE;   // what the panel currently shows
	View pending_view = VIEW_BOOT; // staged by a set_<view>; committed by refresh()
	bool dirty;                    // a setter changed something since the last refresh
	int partials_since_full;
	bool ready; // init() built the widgets; every entry point no-ops until then

	/* What the panel shows temperature in. A display preference; the sensor and the Matter cluster
	 * stay in Celsius. The store that survives a reboot is prefs (src/prefs.cpp) -- this is only what
	 * the widgets are currently painting. */
	ui::TempUnit temp_unit = ui::TempUnit::Celsius;

	/** The unit as it appears next to the number: "°C" / "°F" (0xB0 is in every B612 we generate). */
	const char *unit_token(ui::TempUnit u)
	{
		return u == ui::TempUnit::Fahrenheit ? "\xC2\xB0"
											   "F"
											 : "\xC2\xB0"
											   "C";
	}

	/* Skip-refresh dedup. The last_* sentinels are int, not the API's uint8_t,
	 * because they use -1 for "nothing shown yet".
	 *
	 * last_temp_x100 is in hundredths of the DISPLAYED unit, not of Celsius -- see set_sensor(). */
	bool have_last_reading;
	uint16_t last_co2_ppm, last_hum_x100;
	int32_t last_temp_x100;

	/* The reading itself, as the sensor gave it (Celsius). The dedup key above is in whatever unit
	 * is on the panel, so it cannot be converted back -- and set_temp_unit() has to repaint the
	 * number immediately, not leave the old one standing under the new unit until the next
	 * measurement arrives half a minute later. */
	int32_t last_temp_c_x100;
	int last_batt_pct = -1;
	int last_lowbat_pct = -1;
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

	/** Everything refresh() needs to know about a view: where its container lands, and whether the
	 * user steps through it with the button.
	 *
	 * `transient` decides the refresh kind. A full refresh is a ~2 s black flash; paying it on every
	 * step makes the device feel broken, so transient views are entered and navigated with partial
	 * refreshes, and the ghosting they leave -- the inverted cursor bar and the big DSEG7 digits are
	 * the worst of it -- is cleared by the one full refresh on the way back out to a resting view.
	 *
	 * ONE table, and every view is a row in it, enforced below. Both facts used to be kept by hand --
	 * a chain of ORs for the refresh kind, a switch for the container -- and the chain is how two
	 * views added later (the Calibrate sub-menu and the number editor) came to flash the panel black
	 * on every step: the list named the transient views and said nothing about the rest, so a new one
	 * was silently RESTING and nothing complained. A view the enum knows about cannot be left out
	 * here; a view left out cannot compile.
	 *
	 * The roots are addresses OF the pointers, because the containers do not exist until init()
	 * builds them. A *root that stays null is a view this build does not have (no radio -> no
	 * pairing view). VIEW_NONE maps to the splash, which is what the panel shows before anyone
	 * has said otherwise.
	 */
	struct ViewDef
	{
		lv_obj_t **root;
		bool transient;
	};
	const ViewDef VIEWS[] = {
		/* VIEW_NONE           */ {&boot_root, false},
		/* VIEW_BOOT           */ {&boot_root, false},	// resting: up until the first reading
		/* VIEW_SENSOR         */ {&sensor_root, false}, // resting: the whole point of the device
		/* VIEW_ERROR          */ {&error_root, false},	// resting: up until the fault clears
		/* VIEW_LOWBAT         */ {&lowbat_root, false}, // resting: up until the battery is not
		/* VIEW_RESET          */ {&reset_root, true},	// a prompt, one button away from gone
		/* VIEW_MENU           */ {&list_root, true},	// stepped through -- every menu, root or not
		/* VIEW_VALUE          */ {&value_root, true},	// stepped through, one tap per step
		/* VIEW_PAIRING        */ {&pair_root, true},	// read and dismissed
		/* VIEW_CALIB_PROMPT   */ {&calib_root, true},	// a prompt
		/* VIEW_CALIB_PROGRESS */ {&calib_root, true},	// a bar redrawing every 5 s for 3 minutes
	};
	static_assert(sizeof(VIEWS) / sizeof(VIEWS[0]) == (size_t)VIEW_COUNT,
				  "a new view must say where it lives and whether the user steps through it -- get "
				  "the second wrong and the panel flashes black on every step, or ghosts forever");

	bool transient(View v) { return VIEWS[v].transient; }
	lv_obj_t *root_for(View v) { return *VIEWS[v].root; }

	/** Show or hide one widget. LVGL only offers add/clear of the flag, which turns every
	 * "show this, hide that" into a ternary that reads backwards. */
	void set_hidden(lv_obj_t *o, bool hidden)
	{
		if (hidden)
		{
			lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
		}
		else
		{
			lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN);
		}
	}

	/** Hide every content view, so refresh() can un-hide exactly one.
	 *
	 * Driven off the VIEWS table, not off a list kept by hand. The hand-kept one silently
	 * omitted the pairing view when it was added, and an un-hidden view sits on top of every
	 * other screen forever -- while the device cheerfully logs "display ok". A view the enum
	 * knows about cannot be forgotten here.
	 */
	void hide_all_content()
	{
		for (int v = VIEW_BOOT; v < VIEW_COUNT; v++)
		{
			if (lv_obj_t *root = root_for((View)v))
			{
				set_hidden(root, true);
			}
		}
	}

	// ---- builders (once, in init); `scr` is the active screen ----

	/** Build the always-visible status bar: battery, bolt, and -- where there is a radio -- the link.
	 *
	 * @param scr        the active screen
	 * @param with_radio whether this build can have a link at all; if not, the right-hand side is
	 *                   left empty rather than filled with a dash that answers a question the device
	 *                   is not being asked
	 */
	void build_status_bar(lv_obj_t *scr, bool with_radio)
	{
		has_radio = with_radio;

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

		// The link, and the only thing said about it: four bars on a shared baseline, ascending.
		// Filled = earned; an empty outline = the level exists but is not reached, which is what makes
		// "one bar" read as one OF four rather than as a lonely rectangle.
		//
		// There used to be a text token here as well -- "TH", "BLE..", "--". It is gone, and nothing
		// replaced it: the bar is either showing a strength or it is showing nothing. "TH" told you
		// which radio protocol the device speaks, which is not a thing anyone standing in front of a
		// CO2 monitor wants to know, and "BLE.." belongs on the pairing screen, which says it properly
		// and in words. An empty right-hand side means there is nothing to report -- not commissioned,
		// not joined, or joined and not yet measured -- and that is the honest amount to say.
		//
		// The sizes are not free choices. A 1 px border needs an interior big enough to still look
		// like a hole on a 1-bit panel: at 4x4 the smallest bar had 2x2 white left in it and read as
		// solid, which made "attached, no signal" indistinguishable from "one bar" -- the two states a
		// person carrying the device around most needs told apart.
		constexpr int BAR_W = 5, BAR_GAP = 2, BAR_STEP = 3, BAR_MIN_H = 6;
		for (int i = 0; i < SIGNAL_BARS; i++)
		{
			const int h = BAR_MIN_H + i * BAR_STEP; // 6, 9, 12, 15
			signal_bar[i] = lv_obj_create(status_bar);
			lv_obj_remove_style_all(signal_bar[i]);
			lv_obj_set_size(signal_bar[i], BAR_W, h);
			lv_obj_set_style_border_color(signal_bar[i], lv_color_black(), 0);
			lv_obj_set_style_bg_color(signal_bar[i], lv_color_black(), 0);
			lv_obj_clear_flag(signal_bar[i], LV_OBJ_FLAG_SCROLLABLE);
			// BOTTOM_RIGHT so they sit on one baseline whatever their height; the -1 lifts them off
			// the divider that closes the bar.
			lv_obj_align(signal_bar[i], LV_ALIGN_BOTTOM_RIGHT,
						 -6 - (SIGNAL_BARS - 1 - i) * (BAR_W + BAR_GAP), -4);
			lv_obj_add_flag(signal_bar[i], LV_OBJ_FLAG_HIDDEN); // until there is a link to measure
		}

		// The Matter mark, left of the bars, and it comes and goes with them: it says what network the
		// bars are measuring. Bars without it would be any radio; it without bars would be a boast.
		constexpr int BARS_W = SIGNAL_BARS * BAR_W + (SIGNAL_BARS - 1) * BAR_GAP; // 26
		link_icon = lv_image_create(status_bar);
		lv_image_set_src(link_icon, &matter_img);
		lv_obj_align(link_icon, LV_ALIGN_BOTTOM_RIGHT, -6 - BARS_W - 6, -2);
		lv_obj_add_flag(link_icon, LV_OBJ_FLAG_HIDDEN);

		// Divider under the bar.
		lv_obj_t *sep = make_divider(status_bar, SCR_W, 1);
		lv_obj_align(sep, LV_ALIGN_BOTTOM_MID, 0, 0);
	}

	/** Build the boot splash: wordmark, tagline, author, build stamp. */
	void build_boot(lv_obj_t *scr, const char *build)
	{
		boot_root = make_view(scr);

		lv_obj_t *logo = make_label(boot_root, &b612_48, CONTENT_W);
		lv_label_set_text(logo, "AirInk");
		lv_obj_align(logo, LV_ALIGN_CENTER, 0, -34);

		lv_obj_t *rule = make_divider(boot_root, 180, 2);
		lv_obj_align(rule, LV_ALIGN_CENTER, 0, 0);

		lv_obj_t *author = make_label(boot_root, &b612_16, CONTENT_W);
		lv_label_set_text(author, "Marcus Voss");
		lv_obj_align(author, LV_ALIGN_CENTER, 0, 26);

		/* Which image is on the board. Two builds of this firmware exist and they look alike
		 * everywhere else; the one moment the panel can say so for free is here. */
		lv_obj_t *variant = make_label(boot_root, &b612_16, CONTENT_W);
		lv_label_set_text(variant, build ? build : "");
		lv_obj_align(variant, LV_ALIGN_CENTER, 0, 54);

		/* The Matter mark, at a size where it is actually the mark and not a smudge -- which the
		 * status bar's 22 px is only just, and 48 px comfortably is. The splash is the one screen with
		 * room to spare and the one moment a badge belongs: it is drawn once, at boot, and then the
		 * panel gets on with the job.
		 *
		 * Only where it is true. A build with no radio speaks no Matter and says so by not saying it
		 * -- the same rule as the status bar and the menu's Matter row. */
		if (has_radio)
		{
			lv_obj_t *badge = lv_image_create(boot_root);
			lv_image_set_src(badge, &matter_big_img);
			lv_obj_align(badge, LV_ALIGN_CENTER, 0, -104);
		}

		// Version above the build stamp, and bigger: it is the thing you actually look for when
		// you want to know what is on the board. The date only settles which build of it.
		lv_obj_t *version = make_label(boot_root, &b612_16, CONTENT_W);
		lv_label_set_text(version, "v" AIRINK_VERSION);
		lv_obj_align(version, LV_ALIGN_BOTTOM_MID, 0, -24);

		lv_obj_t *stamp = make_label(boot_root, &b612_14, CONTENT_W);
		lv_label_set_text(stamp, __DATE__ "  " __TIME__);
		lv_obj_align(stamp, LV_ALIGN_BOTTOM_MID, 0, -6);
	}

	/** Build the sensor view: CO2 on top, humidity and temperature below. */
	void build_sensor(lv_obj_t *scr)
	{
		sensor_root = make_view(scr);

		// Each metric is a cell: big value + small unit on one baseline, caption
		// pinned below. Returns the value label so callers can update it; unit_out, where a caller
		// asks for it, receives the small unit label -- temperature is the one whose unit can change
		// while the device is running.
		auto make_metric = [&](lv_align_t align, int y, int w, int h,
							   const char *unit, const char *name,
							   lv_obj_t **unit_out = nullptr) -> lv_obj_t *
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
			if (unit_out)
			{
				*unit_out = u;
			}
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

		temp_value = make_metric(LV_ALIGN_TOP_RIGHT, cell_top, cell_w, cell_h, unit_token(temp_unit),
								 "Temperature", &temp_unit_lbl);
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
		lv_obj_align(title, LV_ALIGN_CENTER, 0, -10);

		// Tap is the reflex, so tap is the harmless one. Same rule as the calibration prompt.
		lv_obj_t *hint = make_label(reset_root, &b612_16, CONTENT_W);
		lv_label_set_text(hint, "Tap = cancel     Hold = reset");
		lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -10);
	}

	/** Build the menu: LIST_MAX_ROWS labels, a cursor, and the hint under them.
	 *
	 * The full complement of rows whether a given list uses them or not -- they are labels, they cost
	 * a few hundred bytes of pool between them, and the alternative is a widget tree that has to be
	 * torn down and rebuilt every time a list with fewer rows comes up. show_list() hides the ones it
	 * does not need and centres the rest.
	 *
	 * The cursor is created before the labels so they draw on top of it: a selected entry is white
	 * text on the black bar, which is the only way to mark a selection without a glyph B612's ASCII
	 * subset does not have.
	 */
	void build_list(lv_obj_t *scr)
	{
		list_root = make_view(scr);
		list_cursor = make_divider(list_root, CONTENT_W - 32, MENU_ITEM_H);

		for (int i = 0; i < ui::LIST_MAX_ROWS; i++)
		{
			list_item[i] = make_label(list_root, &b612_28, CONTENT_W - 32);
			lv_label_set_text(list_item[i], "");
		}

		lv_obj_t *hint = make_label(list_root, &b612_14, CONTENT_W);
		lv_label_set_text(hint, "Tap = next     Hold = select");
		lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -8);
	}

	/** Build the one-button number editor.
	 *
	 * A title, the number in the same big 7-segment face the readings use, its unit beside it, and
	 * two lines under it: what the number means right now, and what the button does. The value and
	 * the reading share a face on purpose -- it is the same quantity, and the point of the screen is
	 * to make them agree. */
	void build_value_edit(lv_obj_t *scr)
	{
		value_root = make_view(scr);

		value_title = make_label(value_root, &b612_16, CONTENT_W);
		lv_obj_align(value_title, LV_ALIGN_TOP_MID, 0, 14);

		// Number + unit on one baseline, as in the sensor view.
		lv_obj_t *row = lv_obj_create(value_root);
		lv_obj_remove_style_all(row);
		lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
		lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
		lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
		lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
		lv_obj_set_style_pad_column(row, 6, 0);
		lv_obj_align(row, LV_ALIGN_CENTER, 0, -18);

		value_num = make_label(row, &dseg7_48, 0);
		value_unit = make_label(row, &b612_28, 0);
		lv_obj_set_style_translate_y(value_unit,
									 (lv_coord_t)(b612_28.base_line - dseg7_48.base_line), 0);

		value_sub = make_label(value_root, &b612_16, CONTENT_W);
		lv_obj_align(value_sub, LV_ALIGN_CENTER, 0, 40);

		value_hint = make_label(value_root, &b612_14, CONTENT_W);
		lv_obj_align(value_hint, LV_ALIGN_BOTTOM_MID, 0, -8);
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
		lv_label_set_text(hdr, "MATTER");
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

		/* The other half of the same screen: once on a fabric there is nothing to scan, so the QR
		 * gives way to the state. Nothing to do here either -- leaving the network is its own
		 * menu entry, not a gesture hidden on a screen that reads like a status line. */
		pair_state_lbl = make_label(pair_root, &b612_48, CONTENT_W);
		lv_label_set_text(pair_state_lbl, "CONNECTED");
		lv_obj_align(pair_state_lbl, LV_ALIGN_CENTER, 0, -10);
		lv_obj_add_flag(pair_state_lbl, LV_OBJ_FLAG_HIDDEN);

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

int ui::init(const Config &cfg)
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
	// Onboarding codes are what a radio build has and a radio-less one does not -- the same test the
	// menu uses to decide whether there is a Matter row.
	build_status_bar(scr, cfg.pair_qr != nullptr);
	h = log_built("status bar", h);
	build_boot(scr, cfg.build);
	h = log_built("boot", h);
	build_sensor(scr);
	h = log_built("sensor", h);
	build_error(scr);
	h = log_built("error", h);
	build_lowbat(scr);
	h = log_built("lowbat", h);
	build_reset(scr);
	h = log_built("reset", h);
	build_list(scr);
	h = log_built("menu", h);
	build_value_edit(scr);
	h = log_built("value edit", h);
	build_calib(scr);
	h = log_built("calib", h);
	/* Only when there is something to pair over -- otherwise no view, and the QR's draw buffer
	 * (the largest single allocation in the pool) is never made. */
	if (cfg.pair_qr)
	{
		build_pairing(scr, cfg.pair_qr, cfg.pair_manual ? cfg.pair_manual : "");
		log_built("pairing", h);
	}
	ready = true;

	// Boot splash: pending_view is VIEW_BOOT and shown is VIEW_NONE, so refresh()
	// paints it with a full refresh.
	ui::refresh();
	return 0;
}

void ui::show_matter(bool commissioned)
{
	if (!ready || !pair_root)
	{
		return;
	}

	// Scan-me and already-on-the-network are the same screen with one half swapped out.
	const bool show_qr = !commissioned;
	set_hidden(pair_qr_obj, !show_qr);
	set_hidden(pair_code_lbl, !show_qr);
	set_hidden(pair_state_lbl, show_qr);

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

	// Dedup on the DISPLAYED value (CO2 exact ppm, T to what the current unit shows, RH to whole %)
	// so a change below what's shown (e.g. 26.03 -> 26.05 C, or 43.05 -> 43.17 % which both show
	// "43") does not force an e-paper refresh. On the 30 s T+RH cadence a no-change tick then costs
	// ~0.16 mAs (SCD41 read only) instead of a ~3 mAs refresh -- see docs/power-analysis.md.
	//
	// So the key must be in the unit on the panel, not in the sensor's. In Fahrenheit that is whole
	// degrees, which is coarser than the 0.1 C of the other unit -- the same room therefore refreshes
	// LESS often in F, not more.
	const bool fahrenheit = (temp_unit == ui::TempUnit::Fahrenheit);
	const int32_t temp_q =
		fahrenheit ? quantize_temp_f_x100(temp_x100) : quantize_temp_x100(temp_x100);
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
	format_x100(buf, sizeof(buf), temp_q, fahrenheit ? 0 : 1);
	lv_label_set_text(temp_value, buf);

	last_co2_ppm = co2_ppm;
	last_temp_x100 = temp_q;
	last_temp_c_x100 = temp_x100;
	last_hum_x100 = hum_q;
	have_last_reading = true;
	dirty = true;
}

void ui::set_temp_unit(TempUnit u)
{
	if (!ready || u == temp_unit)
	{
		return;
	}
	temp_unit = u;

	lv_label_set_text(temp_unit_lbl, unit_token(u));
	// The Units menu row is NOT touched here. The menu owns its own labels now -- it hands the whole
	// list to show_list() every time it draws -- so a setter that reached in to rewrite one of them
	// would be writing behind its back, and would be the last thing left tying the display to what
	// its entries mean.

	// Repaint the number under the new unit from the reading we kept, so the switch is complete the
	// moment it happens. Without this the panel would show, say, 24.3 with a F next to it until the
	// next measurement landed.
	if (have_last_reading)
	{
		const bool fahrenheit = (u == TempUnit::Fahrenheit);
		last_temp_x100 = fahrenheit ? quantize_temp_f_x100(last_temp_c_x100)
									: quantize_temp_x100(last_temp_c_x100);
		char buf[24];
		format_x100(buf, sizeof(buf), last_temp_x100, fahrenheit ? 0 : 1);
		lv_label_set_text(temp_value, buf);
	}

	dirty = true;
}

ui::TempUnit ui::temp_unit_shown()
{
	return temp_unit;
}

void ui::set_error(const char *title, const char *detail)
{
	if (!ready)
	{
		return;
	}
	pending_view = VIEW_ERROR;

	/* Deduped like every other setter here, and it was the only one that was not.
	 *
	 * A sensor that has stopped answering fails on every tick, and the loop stages the same two lines
	 * every time. Unconditionally dirtying the panel meant a ~2 mAs refresh every 30 s for as long as
	 * the fault lasted -- about +67 uA, which more than DOUBLES the 63 uA idle floor, in the one state
	 * where nobody is looking at the device and the battery has to last until somebody does.
	 *
	 * lv_label already holds what is on the glass, so it is also the record of it. */
	const char *t = title ? title : lv_label_get_text(err_title_lbl);
	const char *d = detail ? detail : "";

	if (strcmp(lv_label_get_text(err_title_lbl), t) != 0)
	{
		lv_label_set_text(err_title_lbl, t);
		dirty = true;
	}
	if (strcmp(lv_label_get_text(err_detail_lbl), d) != 0)
	{
		lv_label_set_text(err_detail_lbl, d);
		dirty = true;
	}
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

	// Clamped here as well as in set_battery(), and not only out of caution: without the upper
	// bound in view the compiler must assume a full int and warns that "%d" may not fit in buf
	// (-Wformat-truncation). Stating the range is cheaper than silencing the warning.
	int pct = (last_batt_pct < 0) ? 0 : last_batt_pct;
	if (pct > 100)
	{
		pct = 100;
	}

	// Fill the ~122px interior of the big frame proportionally.
	lv_obj_set_width(lowbat_fill, (lv_coord_t)(pct * 122 / 100));
	char buf[8];
	snprintf(buf, sizeof(buf), "%d", pct);
	lv_label_set_text(lowbat_pct_lbl, buf);

	last_lowbat_pct = last_batt_pct;
	dirty = true;
}

void ui::set_reset_prompt()
{
	if (!ready)
	{
		return;
	}
	pending_view = VIEW_RESET;
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
	set_hidden(batt_pct_lbl, charging);
	set_hidden(batt_bolt, !charging);

	last_batt_pct = pct;
	last_charging = charging;
	dirty = true;
}

void ui::set_signal_bars(int bars)
{
	if (!ready || !has_radio)
	{
		return; // nothing to report, and nowhere it would be drawn
	}
	if (bars > SIGNAL_BARS)
	{
		bars = SIGNAL_BARS;
	}
	if (bars == last_bars)
	{
		return; // the corner already looks exactly like this
	}

	// -1 clears the corner entirely: the mark and the bars come and go together, because the mark
	// says what network the bars are measuring. Bars without it would be any radio; it without bars
	// would be a boast.
	const bool shown = (bars >= 0);
	for (int i = 0; i < SIGNAL_BARS; i++)
	{
		set_hidden(signal_bar[i], !shown);
		// Earned bars are solid; the rest are hollow, so the scale stays visible.
		const bool on = (i < bars);
		lv_obj_set_style_bg_opa(signal_bar[i], on ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
		lv_obj_set_style_border_width(signal_bar[i], on ? 0 : 1, 0);
	}
	set_hidden(link_icon, !shown);

	last_bars = bars;
	dirty = true;
}

void ui::show_list(const char *const *labels, int count, int selected)
{
	if (!ready || count < 1 || count > LIST_MAX_ROWS)
	{
		return; // a list that does not fit is a bug, and drawing it over the hint would hide the bug
	}

	pending_view = VIEW_MENU;

	const int top = menu_top(count);

	// Re-place the block only when the row count changed -- which happens when a different list comes
	// up, and when a build has no Matter row.
	if (count != list_rows)
	{
		for (int i = 0; i < LIST_MAX_ROWS; i++)
		{
			set_hidden(list_item[i], i >= count);
			lv_obj_set_pos(list_item[i], 16, top + i * MENU_ITEM_H + (MENU_ITEM_H - 28) / 2);
		}
		lv_obj_set_x(list_cursor, 16);
		list_rows = count;
		list_sel = -1; // force the cursor and the colours below
		dirty = true;
	}

	// The labels. lv_label already holds the last string, so it is also the record of what is on the
	// panel: comparing against it is what keeps a menu that has not changed from costing a refresh.
	for (int i = 0; i < count; i++)
	{
		if (strcmp(lv_label_get_text(list_item[i]), labels[i]) != 0)
		{
			lv_label_set_text(list_item[i], labels[i]);
			dirty = true;
		}
	}

	if (selected != list_sel)
	{
		for (int i = 0; i < count; i++)
		{
			lv_obj_set_style_text_color(list_item[i],
										(i == selected) ? lv_color_white() : lv_color_black(), 0);
		}
		lv_obj_set_y(list_cursor, top + selected * MENU_ITEM_H);
		list_sel = selected;
		dirty = true;
	}
}

void ui::set_value_edit(const char *title, const char *value, const char *unit, const char *sub,
						const char *hint)
{
	if (!ready)
	{
		return;
	}
	pending_view = VIEW_VALUE;

	lv_label_set_text(value_title, title ? title : "");
	lv_label_set_text(value_num, value ? value : "");
	lv_label_set_text(value_unit, unit ? unit : "");
	lv_label_set_text(value_sub, sub ? sub : "");
	lv_label_set_text(value_hint, hint ? hint : "");

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
