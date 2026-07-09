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

/** Render every screen once and write the PNG/BMP pairs.
 *
 * @retval 0 all screens rendered
 * @retval 1 ui::init() failed
 */
int main()
{
	lv_init();
	lv_tick_set_cb(tick_cb);

	static uint8_t buf[W * H];
	lv_display_t *disp = lv_display_create(W, H);
	lv_display_set_color_format(disp, LV_COLOR_FORMAT_L8);
	lv_display_set_buffers(disp, buf, nullptr, sizeof(buf), LV_DISPLAY_RENDER_MODE_FULL);
	lv_display_set_flush_cb(disp, flush_cb);

	if (ui::init() != 0)
	{
		std::printf("ui::init failed\n");
		return 1;
	}
	// Every frame: stage the changes, then one refresh() commits (as on device).
	ui::set_battery(87, /*charging=*/true); // boot: show the charging bolt
	ui::set_link(ui::Link::BleConnected);
	ui::refresh();
	snapshot("boot");

	g_tick_ms += 100;
	ui::set_sensor(842, 2345, 4500); // 842 ppm, 23.4 C, 45 %
	ui::refresh();
	snapshot("sensor");

	g_tick_ms += 100;
	ui::set_link(ui::Link::ZigbeeConnected); // link token -> ZB
	ui::set_battery(42, false);				 // bolt -> percentage
	ui::set_sensor(1487, 2680, 6200);		 // wide values
	ui::refresh();
	snapshot("sensor_high");

	g_tick_ms += 100;
	ui::set_battery(4, false); // the warning draws this level
	ui::set_low_battery();
	ui::refresh();
	snapshot("lowbat");

	g_tick_ms += 100;
	ui::set_reset(3); // factory-reset countdown
	ui::refresh();
	snapshot("reset");

	g_tick_ms += 100;
	ui::set_error("SENSOR ERROR", "SCD41 not responding");
	ui::refresh();
	snapshot("error");

	std::printf("done\n");
	return 0;
}
