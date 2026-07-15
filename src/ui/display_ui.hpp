#pragma once
#include <stdint.h>

/** @file
 * LVGL UI for the AirInk 4.2" 400x300 e-paper. A persistent status bar on top; below it exactly
 * one content view at a time.
 *
 * The API is staging + commit: every set_*() only updates retained widgets (no panel I/O),
 * set_<view>() also selects the view, and one refresh() pushes ONE e-paper refresh for everything
 * staged -- a measurement cycle costs one refresh, not one per update. Setters dedup at display
 * resolution, because every avoided refresh is ~3 mAs.
 *
 * If init() fails (no display), everything below is a safe no-op.
 */
namespace ui
{
	/** Menu length limit. The panel decides this -- five 44 px rows fit above the hint line -- and
	 * show_list() refuses anything longer rather than drawing rows over each other. */
	constexpr int LIST_MAX_ROWS = 5;

	/** The unit the panel shows temperature in. A display preference only: the sensor and the
	 * Matter cluster stay Celsius. Persisted by prefs, not here. */
	enum class TempUnit : uint8_t
	{
		Celsius,
		Fahrenheit
	};

	/** What this build brought. Which menu rows exist is NOT the display's business -- it draws
	 * whatever list it is handed. */
	struct Config
	{
		/** Named on the boot splash, so a glance says which image is on the board. */
		const char *build = "Standalone";

		/** Matter onboarding payload ("MT:..."), rendered as a QR. NULL in a build with no radio:
		 * then there is no pairing view, no QR draw buffer (the pool's largest allocation), no
		 * signal bars, no Matter mark. */
		const char *pair_qr = nullptr;

		/** The same code for humans, drawn under the QR. Ignored if pair_qr is NULL. */
		const char *pair_manual = nullptr;
	};

	/** Build all widgets (resident, once) and show the boot splash.
	 * @retval -1 no display; every other function becomes a no-op */
	int init(const Config &cfg = {});

	/** The Matter view: the QR while there is something to scan, "CONNECTED" once there is not.
	 * No-op when init() was given no codes. */
	void show_matter(bool commissioned);

	/** Battery indicator (status bar, every view). Charging shows the bolt instead of the number. */
	void set_battery(uint8_t pct, bool charging);

	/** Show the sensor view with whatever readings it holds. The one view the device returns to --
	 * also how a menu or an error is dismissed. Selecting is separate from filling: a reading that
	 * arrives while another view is up must not yank the screen. */
	void show_sensor();

	/** Stage the readings (Celsius; the display converts). Deduped at displayed resolution, so a
	 * change too small to show costs no refresh. */
	void set_sensor(uint16_t co2_ppm, int32_t temp_x100, uint16_t hum_x100);

	/** Change the displayed unit, effective immediately -- the number is repainted from the kept
	 * reading, so the panel never shows a Celsius figure under an F. Does not persist anything. */
	void set_temp_unit(TempUnit u);

	/** Signal bars, the panel's whole radio vocabulary: -1 draws nothing (not joined, or not yet
	 * measured -- four hollow outlines would claim "attached, no signal", a worse state), 0 draws
	 * exactly those four hollow outlines, 1..4 fill that many. Who earns how many is the caller's
	 * judgement (net quantizes with hysteresis); the panel dedups on the count. */
	void set_signal_bars(int bars);

	/** The error view: a headline and a detail line, both required. */
	void set_error(const char *title, const char *detail);

	/** The factory-reset confirmation. A reset drops every fabric and cannot be undone, so the
	 * destructive answer is the deliberate gesture (hold), not the reflex (tap). */
	void set_reset_prompt();

	/** The low-battery view. Draws the level last given to set_battery() -- call that first. */
	void set_low_battery();

	/** The menu view: `count` rows, cursor on `selected` (drawn in reverse video). ONE menu view
	 * draws every list it is handed -- whose rows they are is menu.cpp's business. The whole list
	 * every time (a row like "Altitude: 300 m" has no other way to change); unchanged labels and
	 * cursor cost nothing. Lists longer than LIST_MAX_ROWS are refused. */
	void show_list(const char *const *labels, int count, int selected);

	/** The one-button number editor: title, the value being turned, its unit, an optional line
	 * under it, and what the button does. The sub line is what makes one button bearable -- for
	 * the offset it predicts the reading, so the user never does arithmetic. */
	void set_value_edit(const char *title, const char *value, const char *unit, const char *sub,
						const char *hint);

	/** The calibration prompt. Forced recalibration in the wrong air miscalibrates the sensor for
	 * good; this screen is the whole safety net. */
	void set_calib_prompt();

	/** Calibration progress, 0..100. A bar, not seconds: every redraw is a whole panel refresh, so
	 * updates come every few seconds -- a bar promises nothing but progress. */
	void set_calib_progress(uint8_t pct);

	/** Commit every staged change with a single e-paper refresh: full on a view change and
	 * periodically (ghosting), partial otherwise. Does nothing if nothing changed. */
	void refresh();

	/** Log the LVGL pool (used/peak/free). Views are resident, so a pool that fits at boot fits
	 * forever -- and running it dry is an MPU fault, not a graceful failure. Read this line after
	 * adding a view or a font. No-op in the host sim, whose numbers would be lies. */
	void log_pool(const char *tag);

} // namespace ui
