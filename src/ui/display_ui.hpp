#pragma once
#include <stdint.h>

/** @file
 * LVGL UI for the AirInk 4.2" 400x300 e-paper.
 *
 * A persistent status bar (battery + link) sits on top; below it one content view
 * is shown at a time.
 *
 * The API is staging + commit: every set_*() just updates the retained widgets
 * (no panel I/O), and set_<view>() also selects the active view. A single
 * refresh() then pushes ONE e-paper refresh for all staged changes — so a
 * measurement cycle costs one refresh, not one per update.
 *
 * If init() fails (no display), every function below is a safe no-op — callers
 * do not need to guard their ui:: calls.
 */
namespace ui
{

	// Connectivity shown in the status bar.
	enum class Link
	{
		None,
		BleAdv,
		BleConnected,
		ZigbeeJoining,
		ZigbeeConnected,
		ThreadJoining,
		ThreadConnected
	};

	/** Settings-menu entries, in the order they are drawn.
	 * Units and a factory reset belong here too, but both need a persistent store
	 * that does not exist yet. A calibration lives inside the sensor, so it needs none.
	 */
	enum class Menu : uint8_t
	{
		Calibrate,
		Exit,
		Count
	};

	/** Build all widgets and show the boot splash (one full refresh).
	 *
	 * @retval 0   the panel is ready and the splash is on screen
	 * @retval -1  no display; every other function here becomes a no-op
	 */
	int init();

	/** Stage the battery indicator, shown on every view.
	 *
	 * @param pct      state of charge, 0..100 (clamped)
	 * @param charging true to show the bolt instead of the number
	 */
	void set_battery(uint8_t pct, bool charging);

	/** Stage the connectivity token, shown on every view.
	 *
	 * @param state radio state; ui::Link::None draws "--"
	 */
	void set_link(Link state);

	/** Show the sensor view, with whatever readings it already holds.
	 *
	 * The one view the device returns to, so this is also how a menu, an error or the
	 * low-battery warning is dismissed. Selecting a view is separate from filling it:
	 * a measurement that arrives while another view is up must not yank the screen.
	 */
	void show_sensor();

	/** Stage the three readings, without selecting the view -- see show_sensor().
	 * Values are deduped at display resolution, so a change too small to be shown
	 * costs no e-paper refresh.
	 *
	 * @param co2_ppm   CO2 concentration in ppm
	 * @param temp_x100 temperature in hundredths of a degree Celsius (may be negative)
	 * @param hum_x100  relative humidity in hundredths of a percent
	 */
	void set_sensor(uint16_t co2_ppm, int32_t temp_x100, uint16_t hum_x100);

	/** Select the error view and stage its text.
	 *
	 * @param title  headline, e.g. "SENSOR ERROR"; NULL keeps the previous one
	 * @param detail second line; NULL clears it
	 */
	void set_error(const char *title, const char *detail);

	/** Select the factory-reset view and stage the countdown.
	 *
	 * @param seconds_left seconds remaining before the reset fires
	 */
	void set_reset(uint8_t seconds_left);

	/** Select the low-battery warning view.
	 * It is another view of the same battery as the status bar, so it draws the
	 * level last given to set_battery() -- call that first.
	 */
	void set_low_battery();

	/** Select the settings menu and stage the highlighted entry.
	 *
	 * @param selected the entry to draw in reverse video
	 */
	void set_menu(Menu selected);

	/** Select the calibration view: explain the one prerequisite, offer the way out.
	 *
	 * Forced recalibration teaches the sensor that the air it currently smells is
	 * fresh outdoor air. Run indoors it miscalibrates the sensor for good, so this
	 * screen is the whole safety net -- there is no second confirmation.
	 */
	void set_calib_prompt();

	/** Stage the calibration progress.
	 *
	 * The SCD41 runs periodic measurement for three minutes: the datasheet wants it
	 * measuring the target air in its normal operating mode before a forced
	 * recalibration.
	 *
	 * A bar, not a seconds countdown. Every redraw costs a whole panel refresh, so the
	 * display can only be updated every few seconds -- and a number showing seconds
	 * promises to tick every one of them. A bar promises nothing but progress.
	 *
	 * @param pct how far along, 0..100 (clamped)
	 */
	void set_calib_progress(uint8_t pct);

	/** Commit every staged change with a single e-paper refresh.
	 * Full refresh on a view change and periodically to clear ghosting, partial
	 * otherwise. Does nothing if no setter changed anything.
	 */
	void refresh();

	/** Log how full the LVGL pool is.
	 * Every view is built once and kept resident, so a pool that fits at boot fits
	 * forever -- and running it dry is an MPU fault, not a graceful failure. The peak
	 * also covers the transient glyph draw buffers a render allocates (b612_48 alone is
	 * ~2 KB), which is the figure that says whether the pool is generously sized.
	 *
	 * A no-op wherever the pool cannot be weighed: the host sim runs LVGL on a 16 MB
	 * malloc heap with 64-bit pointers, so a figure from there would be a
	 * plausible-sounding lie.
	 *
	 * @param tag what had just happened, for the log line
	 */
	void log_pool(const char *tag);

} // namespace ui
