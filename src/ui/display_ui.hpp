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

	/** What the radio is doing.
	 *
	 * The status bar draws exactly one of these -- ThreadConnected, as signal bars. Everything else
	 * means "no bars", and the bar is left empty rather than filled with a word.
	 *
	 * It used to print a token for each: "BLE..", "TH", "--". They are gone. "TH" told you which
	 * protocol the device speaks, which is not something anyone standing in front of a CO2 monitor
	 * wants to know, and the pairing screen says the BLE part properly and in words. The states
	 * themselves stay because the radio really is in them and app_task reports them.
	 */
	enum class Link
	{
		None,
		BleAdv,
		BleConnected,
		ThreadJoining,
		ThreadConnected
	};

	/** How many entries a menu may have. The panel decides this, not the menu: five 44 px rows are
	 * what fits above the hint line, and show_list() refuses anything longer rather than drawing over
	 * it. See the static_assert in display_ui.cpp. */
	constexpr int LIST_MAX_ROWS = 5;

	/** The unit the panel shows temperature in.
	 *
	 * A display preference and nothing more. The sensor reads Celsius and the Matter cluster reports
	 * Celsius, because that is what the protocol says; this only decides what is painted. It lives
	 * here rather than in the store that persists it, because the UI is what has units.
	 */
	enum class TempUnit : uint8_t
	{
		Celsius,
		Fahrenheit
	};

	/** What this build of the device can actually do.
	 *
	 * The UI shows what is there and offers nothing that is not: a menu row exists because there
	 * is something behind it, not because of a compile switch. So the caller says what it brought,
	 * and everything else follows from that.
	 */
	struct Config
	{
		/** Named on the boot splash, so a glance at the panel says which image is on the board. */
		const char *build = "Standalone";

		/** Matter onboarding payload ("MT:..."), rendered as a QR. NULL in a build with no radio
		 * -- then there is no Matter row, no view behind it, and no QR draw buffer (the single
		 * largest allocation in the LVGL pool). */
		const char *pair_qr = nullptr;

		/** The same code for humans ("1234-567-8901"), drawn under the QR for when a camera will
		 * not cooperate. Ignored if pair_qr is NULL. */
		const char *pair_manual = nullptr;

		/** Whether anything can actually be reset. If not, the menu does not offer it. */
		bool factory_reset = false;
	};

	/** Build all widgets and show the boot splash (one full refresh).
	 *
	 * @param cfg what this build brought; see Config
	 *
	 * @retval 0   the panel is ready and the splash is on screen
	 * @retval -1  no display; every other function here becomes a no-op
	 */
	int init(const Config &cfg = {});

	/** Select the Matter view.
	 *
	 * Two states, one screen, because they answer the same question -- "is this thing on my
	 * network?". Uncommissioned it shows the QR and the manual code given to init(); commissioned
	 * there is nothing to scan, so it says so, and offers the way back out of the network.
	 * A no-op when init() was given no codes.
	 *
	 * @param commissioned whether the device is already on a fabric
	 */
	void show_matter(bool commissioned);

	/** Stage the battery indicator, shown on every view.
	 *
	 * @param pct      state of charge, 0..100 (clamped)
	 * @param charging true to show the bolt instead of the number
	 */
	void set_battery(uint8_t pct, bool charging);

	/** Stage the radio state, which the status bar carries across every view.
	 *
	 * Nothing is drawn for it except signal bars, and those only on a joined Thread link that has
	 * been measured (set_signal). Any other state empties that corner of the bar.
	 *
	 * @param state radio state
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

	/** Change the unit temperature is shown in. Always pass Celsius readings to set_sensor(); this
	 * decides only what is painted.
	 *
	 * Takes effect at once: the unit next to the number, the number itself (recomputed from the last
	 * reading, so the panel never shows a Celsius figure under an F), and the Units menu row. Like
	 * every other setter it only stages -- ui::refresh() puts it on the glass.
	 *
	 * It does not persist anything. prefs (src/prefs.cpp) is the store; this is the display.
	 */
	void set_temp_unit(TempUnit u);

	/** The unit currently painted. The menu asks, so that a hold can toggle it. */
	TempUnit temp_unit_shown();

	/** How strong the Thread link is, so the status bar can show bars instead of a bare "TH".
	 *
	 * "TH" answers a question nobody has once the device is joined. The one they do have -- is it
	 * standing somewhere it can actually reach the mesh? -- needs a strength, and this is it.
	 *
	 * The bars only appear on a joined Thread link (see set_link), and only once this has been called
	 * at least once: four hollow bars mean "attached with no signal", which must not be confused with
	 * "not measured yet".
	 *
	 * Deduped and hysteretic (ui::quantize_signal_bars): a link parked on a threshold does not get to
	 * flip the panel back and forth at ~3 mAs a go.
	 *
	 * @param rssi_dbm average RSSI to the parent router, in dBm (negative)
	 */
	void set_signal(int rssi_dbm);

	/** How many bars are drawn, 0..4, or -1 if nothing has been measured yet.
	 *
	 * The hysteresis lives in here, so this is the only place that knows whether a new reading
	 * actually moved the display -- which is what the log wants to say, and the only thing worth
	 * saying about a number that drifts by a dB every half minute.
	 */
	int signal_bars();

	/** Select the error view and stage its text.
	 *
	 * @param title  headline, e.g. "SENSOR ERROR"; NULL keeps the previous one
	 * @param detail second line; NULL clears it
	 */
	void set_error(const char *title, const char *detail);

	/** Select the factory-reset confirmation.
	 *
	 * A factory reset drops every fabric: the device leaves the network it is on, and whoever put
	 * it there has to put it back. That is not undoable, and this device has one button -- so it
	 * gets the same safety net as a recalibration, a screen that says what is about to happen and
	 * makes the destructive answer the deliberate one (hold), not the reflex (tap).
	 */
	void set_reset_prompt();

	/** Select the low-battery warning view.
	 * It is another view of the same battery as the status bar, so it draws the
	 * level last given to set_battery() -- call that first.
	 */
	void set_low_battery();

	/** Select the menu view and stage its rows, with the cursor on one of them.
	 *
	 * There is ONE menu view, and it draws whatever list it is handed. It has no idea that a root
	 * menu and a Calibrate sub-menu exist -- to the panel they are five strings and a cursor, twice,
	 * and a second set of widgets for the second one would be a second copy of the first for no
	 * reason. (There was one, briefly. It cost 1.7 KB of an LVGL pool that has 5 KB left, and it made
	 * stepping into the sub-menu look like a view change, which on e-paper means a black flash.)
	 *
	 * Whose rows these are, and where the cursor was left in each, is menu.cpp's business.
	 *
	 * The whole list every time, labels and all -- a row that carries a value ("Altitude: 300 m") has
	 * no other way to change. Nothing is redrawn that has not moved: an unchanged label and an
	 * unchanged cursor leave the panel alone.
	 *
	 * @param labels   `count` strings, top to bottom; they need not outlive the call
	 * @param count    how many rows, 1..LIST_MAX_ROWS (more is a programming error and is ignored)
	 * @param selected the row drawn in reverse video, 0..count-1
	 */
	void show_list(const char *const *labels, int count, int selected);

	/** Select the one-button number editor: a title, the value being turned, and what a tap does.
	 *
	 * One button cannot really enter a number, so it does not pretend to: a tap steps the value and
	 * wraps at the end, and a hold keeps it. What makes that bearable is the second line -- for the
	 * temperature offset it carries the reading as it stands right now, so the user turns the value
	 * until the panel agrees with the thermometer in their hand, and never has to do arithmetic.
	 *
	 * @param title the setting, e.g. "TEMP OFFSET"
	 * @param value the number as it should be read, e.g. "4.0"
	 * @param unit  what it is measured in, e.g. "\xC2\xB0" "C" or "m"
	 * @param sub   the line under it; NULL for none
	 * @param hint  what the buttons do, e.g. "Tap = +0.5     Hold = save"
	 */
	void set_value_edit(const char *title, const char *value, const char *unit, const char *sub,
						const char *hint);

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
