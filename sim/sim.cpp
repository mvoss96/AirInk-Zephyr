/** @file
 * Host preview harness for the AirInk LVGL UI (see build.ps1).
 *
 * Compiles the real UI (src/ui/display_ui.cpp) against LVGL on the PC, renders
 * each screen into an L8 buffer via a headless flush callback, thresholds it to pure
 * black/white (to emulate the 1-bit e-paper) and writes one PNG per screen.
 *
 * The PNG encoder is deliberately kept in its own png_writer.h so it can be
 * swapped/dropped independently (see the note there).
 */
#include <lvgl.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "ui/display_ui.hpp"
#include "png_writer.h"
#include "bmp_writer.h"

// The platform seam (plat::) is implemented in ui_platform_sim.cpp.

// ---- headless LVGL display + snapshot ----
namespace
{

	constexpr int W = 400, H = 300; // landscape geometry, matches display_ui.cpp
	uint8_t g_fb[W * H];			// captured L8 frame (0=black..255=white)
	uint32_t g_tick_ms = 0;

	/** LVGL's clock source. @return the fake time in milliseconds */
	uint32_t tick_cb() { return g_tick_ms; }

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

	/* Exactly what each build hands ui::init() on the device. The Matter payload is a real one
	 * from the board, so the QR in the mockup is the QR on the panel. The standalone build hands
	 * over nothing -- and that absence is what takes the Matter and Factory reset rows out of its
	 * menu, and the QR's draw buffer out of its LVGL pool. */
	const ui::Config cfg = matter ? ui::Config{
										.build = "Matter over Thread",
										.pair_qr = "MT:M1TJ342C00KA0648G00",
										.pair_manual = "3535-860-0323",
										.factory_reset = true,
									}
								  : ui::Config{};
	if (ui::init(cfg) != 0)
	{
		std::printf("ui::init failed\n");
		return 1;
	}
	// The radio state the status bar would really show: Thread once paired, nothing at all in a
	// build that has no radio to report on.
	const ui::Link link = matter ? ui::Link::ThreadConnected : ui::Link::None;

	// Every frame: stage the changes, then one refresh() commits (as on device).
	ui::set_battery(87, /*charging=*/true); // boot: show the charging bolt
	ui::set_link(link);
	ui::refresh();
	snapshot("boot");

	// show_sensor() selects the view, set_sensor() fills it -- as the loop's measure() does.
	g_tick_ms += 100;
	ui::show_sensor();
	ui::set_sensor(842, 2345, 4500); // 842 ppm, 23.4 C, 45 %
	ui::refresh();
	snapshot("sensor");

	// The signal bars, every level, because the whole point of them is being read at a glance from
	// across a room -- and one bar has to look unmistakably unlike four. Only the radio build has a
	// mesh to measure; the other keeps its "--".
	if (matter)
	{
		const struct
		{
			int rssi;
			const char *name;
		} levels[] = {
			{-55, "signal_4"},	// next to the router
			{-70, "signal_3"},	//
			{-80, "signal_2"},	//
			{-90, "signal_1"},	// on the edge
			{-100, "signal_0"}, // attached, and barely
		};
		for (const auto &l : levels)
		{
			g_tick_ms += 100;
			ui::set_signal(l.rssi);
			ui::refresh();
			snapshot(l.name);
		}
		g_tick_ms += 100;
		ui::set_signal(-55); // back to a healthy link for the screens that follow
		ui::refresh();
	}

	g_tick_ms += 100;
	ui::set_battery(42, false);		  // bolt -> percentage
	ui::set_sensor(1487, 2680, 6200); // wide values, view already selected
	ui::refresh();
	snapshot("sensor_high");

	// The same reading in the other unit. Whole degrees in Fahrenheit, one decimal in Celsius -- the
	// panel shows what the sensor can distinguish in the unit on screen, and 0.1 F it cannot. Note
	// the number is NOT re-measured: set_temp_unit repaints from the reading it kept, which is
	// exactly what a user pressing the button in a still room sees.
	g_tick_ms += 100;
	ui::set_temp_unit(ui::TempUnit::Fahrenheit);
	ui::refresh();
	snapshot("sensor_fahrenheit");

	g_tick_ms += 100;
	ui::set_temp_unit(ui::TempUnit::Celsius);
	ui::refresh();

	g_tick_ms += 100;
	ui::set_battery(4, false); // the warning draws this level
	ui::set_low_battery();
	ui::refresh();
	snapshot("lowbat");

	// The menu. It is now a list of strings with a cursor -- the display does not know what the
	// entries mean, so the sim says them out loud, exactly as menu.cpp's tables would.
	g_tick_ms += 100;
	ui::set_battery(87, false);

	const char *root_matter[] = {"Calibrate", "Units: °" "C", "Matter", "Factory reset", "Exit"};
	const char *root_plain[] = {"Calibrate", "Units: °" "C", "Exit"};
	const char *const *root = matter ? root_matter : root_plain;
	const int root_n = matter ? 5 : 3;

	for (int i = 0; i < root_n; i++)
	{
		g_tick_ms += 100;
		ui::show_list(root, root_n, i);
		ui::refresh();
		char nm[24];
		std::snprintf(nm, sizeof(nm), "menu_%d", i);
		snapshot(nm);
	}

	// The Units row carries the unit in force -- the only place the setting is visible at all.
	g_tick_ms += 100;
	const char *root_f[] = {"Calibrate", "Units: °" "F", "Matter", "Factory reset", "Exit"};
	ui::set_temp_unit(ui::TempUnit::Fahrenheit);
	ui::show_list(root_f, root_n, 1);
	ui::refresh();
	snapshot("menu_units_f");
	g_tick_ms += 100;
	ui::set_temp_unit(ui::TempUnit::Celsius);

	// The Calibrate sub-menu: the same list widget, a second time, for nothing.
	const char *cal[] = {"Recalibrate CO2", "Temp offset: 6.5 °" "C", "Altitude: 300 m",
						 "Auto-calib: Off", "Back"};
	for (int i = 0; i < 5; i++)
	{
		g_tick_ms += 100;
		ui::show_list(cal, 5, i);
		ui::refresh();
		char nm[24];
		std::snprintf(nm, sizeof(nm), "calmenu_%d", i);
		snapshot(nm);
	}

	// The one editor, behind any row that carries a number.
	g_tick_ms += 100;
	ui::set_value_edit("TEMP OFFSET", "4.0", "°" "C", "Will read 23.5 °" "C",
					   "Tap = +0.5     Hold = save");
	ui::refresh();
	snapshot("edit_offset");

	g_tick_ms += 100;
	ui::set_value_edit("TEMP OFFSET", "6.5", "°" "C", "Will read 21.0 °" "C",
					   "Tap = +0.5     Hold = save");
	ui::refresh();
	snapshot("edit_offset_turned");

	g_tick_ms += 100;
	ui::set_value_edit("ALTITUDE", "300", "m", "Above sea level", "Tap = +100     Hold = save");
	ui::refresh();
	snapshot("edit_altitude");

	if (matter)
	{
		// The Matter screen, both halves. Uncommissioned: something to scan, and the radio is
		// advertising over BLE. Commissioned: nothing to scan, so the state instead.
		g_tick_ms += 100;
		ui::set_link(ui::Link::BleAdv);
		ui::show_matter(/*commissioned=*/false);
		ui::refresh();
		snapshot("matter_pairing");

		g_tick_ms += 100;
		ui::set_link(link);
		ui::show_matter(/*commissioned=*/true);
		ui::refresh();
		snapshot("matter_connected");

		// Reached by holding on the Factory reset row. Tap cancels; hold drops every fabric.
		g_tick_ms += 100;
		ui::set_reset_prompt();
		ui::refresh();
		snapshot("reset_prompt");
	}

	// The calibration flow, one snapshot per step the user walks through.
	g_tick_ms += 100;
	ui::set_calib_prompt();
	ui::refresh();
	snapshot("calib_prompt");

	g_tick_ms += 100;
	ui::set_calib_progress(35);
	ui::refresh();
	snapshot("calib_progress");

	// The full bar the user sees during the CO2 read that follows the recalibration.
	g_tick_ms += 100;
	ui::set_calib_progress(100);
	ui::refresh();
	snapshot("calib_done");

	// Staged by the menu itself, and dismissed by any gesture -- hence the hint line.
	g_tick_ms += 100;
	ui::set_error("CALIBRATION FAILED", "PRESS TO CONTINUE");
	ui::refresh();
	snapshot("calib_failed");

	g_tick_ms += 100;
	ui::set_error("SENSOR ERROR", "SCD41 not responding");
	ui::refresh();
	snapshot("error");

	std::printf("done\n");
	return 0;
}
