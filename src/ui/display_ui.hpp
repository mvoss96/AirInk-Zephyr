#pragma once
#include <stdint.h>

/*
 * LVGL UI for the AirInk 4.2" 400x300 e-paper. A persistent status bar (battery
 * + link) sits on top; below it one content view is shown at a time.
 *
 * The API is staging + commit: every set_*() just updates the retained widgets
 * (no panel I/O), and set_<view>() also selects the active view. A single
 * refresh() then pushes ONE e-paper refresh for all staged changes — so a
 * measurement cycle costs one refresh, not one per update.
 */
namespace ui
{

	/* Connectivity shown in the status bar. */
	enum class Link
	{
		None,
		BleAdv,
		BleConnected,
		ZigbeeJoining,
		ZigbeeConnected
		/* , Thread… */
	};

	/* Build all widgets, show the boot splash (one full refresh). 0 on success. */
	int init();

	/* Status-bar data (visible on every view). Stage only — call refresh() to show. */
	void set_battery(uint8_t percent, bool charging = false);
	void set_link(Link state);

	/* Select the active content view + its data. Stage only — call refresh(). */
	void set_sensor(uint16_t co2_ppm, int32_t temp_c_x100, uint16_t hum_x100);
	void set_error(const char *title, const char *detail);
	void set_low_battery(uint8_t percent);
	void set_reset(uint8_t seconds_left); /* factory-reset countdown */

	/* Commit staged changes with a single refresh (full on a view change or
	 * periodically to clear ghosting; partial otherwise; no-op if unchanged). */
	void refresh();

} // namespace ui
