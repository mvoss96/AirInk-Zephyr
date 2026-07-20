/** @file
 * Host preview harness for the AirInk LVGL UI (see build.ps1).
 *
 * Compiles the real UI (src/ui/display_ui.cpp) against LVGL on the PC, renders
 * each screen into an L8 buffer via a headless flush callback, thresholds it to pure
 * black/white (to emulate the 1-bit e-paper) and writes one PNG per screen.
 *
 * The menu screens are not described here, they are WALKED: menu.cpp, prefs.cpp and net.cpp are
 * compiled in (app_stubs.cpp holds what they stand on), so this file taps and holds a button and
 * photographs what comes out. Its lists therefore cannot drift from the device's -- they are the
 * device's. They did drift, back when they were literals here: one still offered a root-menu row
 * that had moved into a sub-menu.
 *
 * The PNG encoder is deliberately kept in its own png_writer.h so it can be
 * swapped/dropped independently (see the note there).
 */
#include <lvgl.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "input/button.hpp"
#include "menu.hpp"
#include "net.hpp"
#include "prefs.hpp"
#include "sim_host.hpp"
#include "ui/display_ui.hpp"
#include "png_writer.h"
#include "bmp_writer.h"

// The platform seam (plat::) is implemented in ui_platform_sim.cpp.

// ---- headless LVGL display + snapshot ----
namespace
{

	constexpr int W = 400, H = 300; // landscape geometry, matches display_ui.cpp
	uint8_t g_fb[W * H];			// captured L8 frame (0=black..255=white)

	/** LVGL's clock source -- the same one k_uptime_get() answers from (app_stubs.cpp), so a menu
	 * timing out and an animation running agree on what a second was.
	 * @return the fake time in milliseconds */
	uint32_t tick_cb() { return simhost::tick_ms; }

	/** Headless flush: copy LVGL's rendered area into g_fb instead of a panel.
	 *
	 * @param disp   the display being flushed; must be told when we are done
	 * @param area   the rectangle LVGL rendered, in screen coordinates
	 * @param px_map its pixels, one L8 byte each, row-major within the area
	 */
	void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
	{
		const int aw = area->x2 - area->x1 + 1;
		for (int y = area->y1; y <= area->y2; y++)
			for (int x = area->x1; x <= area->x2; x++)
				g_fb[y * W + x] = px_map[(y - area->y1) * aw + (x - area->x1)];
		lv_display_flush_ready(disp);
	}

	/** Threshold the captured frame to pure black/white and write it out.
	 *
	 * @param name basename of the pair to write: <name>.png and <name>.bmp
	 */
	void snapshot(const char *name)
	{
		static uint8_t bw[W * H];
		for (int i = 0; i < W * H; i++)
			bw[i] = (g_fb[i] >= 128) ? 255 : 0;

		char path[64];
		std::snprintf(path, sizeof(path), "%s.png", name);
		int rc = write_gray_png(path, bw, W, H);
		std::snprintf(path, sizeof(path), "%s.bmp", name);
		rc |= write_gray_bmp(path, bw, W, H);

		std::printf(rc == 0 ? "wrote %s.{png,bmp}\n" : "FAILED %s\n", name);
	}

	// ---- driving the real menu ----

	/** FNV-1a over the thresholded frame. Only ever compared, never stored: it answers "is this the
	 * same picture as before", which is how a walk knows a list has come back around. */
	uint64_t frame_hash()
	{
		uint64_t h = 1469598103934665603ull;
		for (int i = 0; i < W * H; i++)
		{
			h = (h ^ (uint64_t)(g_fb[i] >= 128)) * 1099511628211ull;
		}
		return h;
	}

	/** One gesture into the menu, then the single refresh that commits whatever it staged -- the
	 * loop's shape (app.cpp), so the preview cannot see a frame the device never draws.
	 * @param hold_ms time to pass first; a big value is how an idle timeout is reached */
	menu::Status gesture(button::Event e, uint32_t hold_ms = 100)
	{
		simhost::tick_ms += hold_ms;
		const menu::Status s = menu::proceed(e);
		ui::refresh();
		return s;
	}

	/** Photograph a list, one shot per row, tapping between them, and stop when the cursor has
	 * wrapped back to where it started. Nothing here knows how long the list is -- menu.cpp's table
	 * does, and a row added or hidden there changes this set of pictures by itself.
	 * @param prefix shots are written <prefix>_0, <prefix>_1, ...
	 * @return how many rows the list showed */
	int walk_list(const char *prefix)
	{
		const uint64_t first = frame_hash();
		for (int i = 0; i < ui::LIST_MAX_ROWS; i++)
		{
			char nm[32];
			std::snprintf(nm, sizeof(nm), "%s_%d", prefix, i);
			snapshot(nm);
			gesture(button::Event::Short);
			if (frame_hash() == first)
			{
				return i + 1;
			}
		}
		return ui::LIST_MAX_ROWS;
	}

	/** Open the menu at the top, cursors reset -- the state every walk below starts from. */
	void to_root()
	{
		simhost::tick_ms += 100;
		menu::enter();
		ui::refresh();
	}

	/** Tap `n` times without photographing: getting to a row, rather than showing it. */
	void tap_to(int n)
	{
		for (int i = 0; i < n; i++)
		{
			gesture(button::Event::Short);
		}
	}

} // namespace

/** Render every screen of one build and write the PNG/BMP pairs.
 *
 * The two builds do not draw the same device: the Matter one names itself on the splash and has
 * two extra menu rows with screens behind them. So the sim renders whichever it was asked for --
 * and build.ps1 asks for both, because a screen that only one build has is exactly the screen a
 * mockup is needed for.
 *
 * @param argc 2
 * @param argv [1] = "standalone" or "matter"
 *
 * @retval 0 all screens rendered
 * @retval 1 bad arguments, or ui::init() failed
 */
int main(int argc, char **argv)
{
	if (argc != 2)
	{
		std::printf("usage: airink_sim <standalone|matter>\n");
		return 1;
	}
	const bool matter = std::strcmp(argv[1], "matter") == 0;
	if (!matter && std::strcmp(argv[1], "standalone") != 0)
	{
		std::printf("unknown build '%s'\n", argv[1]);
		return 1;
	}

	lv_init();
	lv_tick_set_cb(tick_cb);

	static uint8_t buf[W * H];
	lv_display_t *disp = lv_display_create(W, H);
	lv_display_set_color_format(disp, LV_COLOR_FORMAT_L8);
	lv_display_set_buffers(disp, buf, nullptr, sizeof(buf), LV_DISPLAY_RENDER_MODE_FULL);
	lv_display_set_flush_cb(disp, flush_cb);

	/* Exactly what each build hands ui::init() on the device. The Matter payload is a real one from
	 * the board, so the QR in the mockup is the QR on the panel. The standalone build hands over
	 * nothing -- and that absence is what takes the QR's draw buffer out of its LVGL pool, the signal
	 * bars and the Matter mark off its status bar, and the badge off its splash. (Which rows its menu
	 * has is menu.cpp's business, not the display's, so the sim spells them out below.) */
	const ui::Config cfg = matter ? ui::Config{
										.build = "Matter over Thread",
										.pair_qr = "MT:M1TJ342C00KA0648G00",
										.pair_manual = "3535-860-0323",
									}
								  : ui::Config{};
	if (ui::init(cfg) != 0)
	{
		std::printf("ui::init failed\n");
		return 1;
	}

	/* What each build installs before app::run(). net decides from these whether the device HAS a
	 * radio and whether it can drop a fabric -- and the menu asks net exactly that before offering
	 * the Matter and Factory reset rows. So the two builds' menus differ here, once, as on device.
	 * (The hook only has to exist; nothing in a preview is going to reset anything.) */
	static const net::Hooks radio_hooks = {
		.factory_reset = [] {},
	};
	if (matter)
	{
		net::set_pairing_codes(cfg.pair_qr, cfg.pair_manual);
		net::set_hooks(radio_hooks);
	}

	// The store the menu reads its values and ranges out of. There is none on a PC, which prefs
	// survives by design -- so the mockups show a device on factory defaults.
	prefs::init();
	// Every frame: stage the changes, then one refresh() commits (as on device). No signal bars
	// yet: at boot nothing has been measured, and the status bar's corner stays honestly empty.
	ui::set_battery(87, /*charging=*/true); // boot: show the charging bolt
	ui::refresh();
	snapshot("boot");

	// show_sensor() selects the view, set_sensor() fills it -- as the loop's measure() does.
	simhost::tick_ms += 100;
	ui::show_sensor();
	ui::set_sensor(842, 2345, 4500); // 842 ppm, 23.4 C, 45 %
	ui::refresh();
	snapshot("sensor");

	// The signal bars, every level, because the whole point of them is being read at a glance from
	// across a room -- and one bar has to look unmistakably unlike four. Only the radio build has a
	// mesh to measure; the other's corner stays empty. The counts are what net's quantizer would
	// hand over for ~-55, -70, -80, -90 and -100 dBm.
	if (matter)
	{
		const struct
		{
			int bars;
			const char *name;
		} levels[] = {
			{4, "signal_4"}, // next to the router
			{3, "signal_3"}, //
			{2, "signal_2"}, //
			{1, "signal_1"}, // on the edge
			{0, "signal_0"}, // attached, and barely
		};
		for (const auto &l : levels)
		{
			simhost::tick_ms += 100;
			ui::set_signal_bars(l.bars);
			ui::refresh();
			snapshot(l.name);
		}
		simhost::tick_ms += 100;
		ui::set_signal_bars(4); // back to a healthy link for the screens that follow
		ui::refresh();
	}

	simhost::tick_ms += 100;
	ui::set_battery(42, false);		  // bolt -> percentage
	ui::set_sensor(1487, 2680, 6200); // wide values, view already selected
	ui::refresh();
	snapshot("sensor_high");

	// The same reading in the other unit. Whole degrees in Fahrenheit, one decimal in Celsius -- the
	// panel shows what the sensor can distinguish in the unit on screen, and 0.1 F it cannot. Note
	// the number is NOT re-measured: set_temp_unit repaints from the reading it kept, which is
	// exactly what a user pressing the button in a still room sees.
	simhost::tick_ms += 100;
	ui::set_temp_unit(ui::TempUnit::Fahrenheit);
	ui::refresh();
	snapshot("sensor_fahrenheit");

	simhost::tick_ms += 100;
	ui::set_temp_unit(ui::TempUnit::Celsius);
	ui::refresh();

	simhost::tick_ms += 100;
	ui::set_battery(4, false); // the warning draws this level
	ui::set_low_battery();
	ui::refresh();
	snapshot("lowbat");

	/* ---- the menu, walked ------------------------------------------------------------------
	 *
	 * Everything below is photographed out of the real menu: a gesture goes in, whatever menu.cpp
	 * staged comes out. So the rows, their order, their labels and the values baked into them are
	 * the device's, and a table edited in src/ re-shoots these by itself. */
	ui::set_battery(87, /*charging=*/true); // the cable is in, which is what the System row asks
	simhost::charging = true;

	to_root();
	const int root_n = walk_list("menu");

	/* The Units row is the setting: holding it flips the stored unit, and the row rewrites itself
	 * from the store. Nothing here says what "F" looks like -- prefs and the menu do. */
	to_root();
	tap_to(1);
	gesture(button::Event::Long); // C -> F, cursor stays on the row
	snapshot("menu_units_f");
	gesture(button::Event::Long); // and back, so the screens that follow read in Celsius

	// The Calibrate sub-menu (first row of the root), the same list widget a second time.
	to_root();
	gesture(button::Event::Long);
	walk_list("calmenu");

	/* The one editor, behind the two rows that carry a number. The range it wraps in and the value
	 * it starts from come from prefs's table, so these two shots are of the real thing. */
	to_root();
	gesture(button::Event::Long); // into Calibrate
	tap_to(1);					  // Temp offset
	gesture(button::Event::Long);
	snapshot("edit_offset");
	gesture(button::Event::Short); // one step
	snapshot("edit_offset_turned");
	gesture(button::Event::None, 121000); // walked away: the editor discards and goes back

	to_root();
	gesture(button::Event::Long);
	tap_to(2); // Altitude
	gesture(button::Event::Long);
	snapshot("edit_altitude");
	gesture(button::Event::None, 121000);

	/* The System sub-menu: second to last in the root, ahead of Exit. Its Firmware update row is
	 * there only because the cable is -- the bolt in the corner is the condition, not decoration. */
	to_root();
	tap_to(root_n - 2);
	gesture(button::Event::Long);
	walk_list("sysmenu");

	// Held on Firmware update: tap backs out, hold reboots into the UF2 bootloader.
	gesture(button::Event::Long);
	snapshot("update_prompt");
	const menu::Status confirmed = gesture(button::Event::Long);

	/* What the panel keeps showing while the bootloader has the USB drive open. This screen is the
	 * LOOP's line, not the menu's (app.cpp paints it on the status above), which is why the sim
	 * paints it too -- after checking the menu really asked for it. */
	if (confirmed == menu::Status::FirmwareUpdate)
	{
		simhost::tick_ms += 100;
		ui::set_error("FIRMWARE UPDATE", "COPY UF2 TO NICENANO");
		ui::refresh();
		snapshot("update_bootloader");
	}
	else
	{
		std::printf("FAILED update_bootloader: the menu did not confirm the update\n");
	}

	simhost::tick_ms += 100;
	ui::set_battery(87, false);

	if (matter)
	{
		/* The Matter screen, both halves, entered the way the device enters it. Uncommissioned:
		 * something to scan, no Thread yet, so no bars. Commissioned: the state instead. */
		simhost::tick_ms += 100;
		ui::set_signal_bars(-1);
		net::set_commissioned(false);
		menu::enter_matter();
		ui::refresh();
		snapshot("matter_pairing");

		simhost::tick_ms += 100;
		ui::set_signal_bars(4);
		net::set_commissioned(true);
		menu::enter_matter();
		ui::refresh();
		snapshot("matter_connected");

		/* Factory reset, reached with no cable in -- which also shows the System list with its
		 * USB-only row gone, and puts Factory reset where the first row was. */
		simhost::charging = false;
		to_root();
		tap_to(root_n - 2);
		gesture(button::Event::Long);
		snapshot("sysmenu_nousb_0");
		gesture(button::Event::Long); // hold the first row: Factory reset
		snapshot("reset_prompt");
		gesture(button::Event::Short); // tap cancels, as the screen says
		simhost::charging = true;
	}

	// The calibration flow, walked: prompt, the warm-up bar part way, and the full bar that stands
	// during the CO2 read after it. The clock is the menu's own (CALIB_MS), so the bar is honest.
	to_root();
	gesture(button::Event::Long); // Calibrate
	gesture(button::Event::Long); // Recalibrate CO2
	snapshot("calib_prompt");
	gesture(button::Event::Long); // hold confirms: into the warm-up
	gesture(button::Event::None, 60000);
	snapshot("calib_progress");
	gesture(button::Event::None, 121000); // past the three minutes: full bar, then the FRC
	snapshot("calib_done");

	// The verdict when the sensor refuses -- reached by making it refuse, not by painting it.
	simhost::calib_fails = true;
	to_root();
	gesture(button::Event::Long);
	gesture(button::Event::Long);
	gesture(button::Event::Long);
	snapshot("calib_failed");
	simhost::calib_fails = false;

	simhost::tick_ms += 100;
	ui::set_error("SENSOR ERROR", "SCD41 not responding");
	ui::refresh();
	snapshot("error");

	std::printf("done\n");
	return 0;
}
