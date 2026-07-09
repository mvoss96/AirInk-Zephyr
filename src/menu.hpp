#pragma once
#include <stdint.h>

#include "input/button.hpp"

/** @file
 * What the button does, and which view the user is looking at.
 *
 * main.cpp owns the measurement cadence; this module owns everything the user drives.
 * It never measures and never refreshes the panel -- it only stages ui:: views, so the
 * loop keeps its "exactly one panel refresh per iteration" property.
 *
 * Sensor --hold--> Menu --hold(Calibrate)--> CalibPrompt --hold--> CalibRun --3 min--> Sensor
 *   ^               |  tap = next entry          |  tap = back        |  hold = abort
 *   +---------------+  hold(Exit) / 30 s idle ---+--------------------+
 */
namespace menu
{
	// Fresh outdoor air, the only concentration a user can reliably stand in.
	constexpr uint16_t CALIB_TARGET_PPM = 420;

	/** Wire up the way back to the sensor view.
	 *
	 * This module decides *when* the user returns to the readings, but it does not
	 * retain them -- main does. So it asks.
	 *
	 * @param show_sensor_view stages the sensor view with the last known reading
	 */
	void init(void (*show_sensor_view)());

	/** Is the user somewhere other than the sensor view?
	 *
	 * While true, main must not stage the sensor or low-battery views: those would
	 * yank the panel away from whatever the user is reading.
	 */
	bool active();

	/** Is the SCD41 busy being recalibrated?
	 *
	 * While true it sits in periodic measurement and must not be single-shot sampled.
	 * The battery may still be read.
	 */
	bool holds_sensor();

	/** Did a recalibration just complete? Clears the flag.
	 *
	 * The caller uses this to force a full CO2 read on the next tick: the retained
	 * reading was taken with the old calibration.
	 */
	bool take_recalibrated();

	/** Leave whatever the user was doing and return to the sensor view.
	 *
	 * Aborts a running calibration. Used when the battery gets too low to continue.
	 */
	void force_exit();

	/** Advance the state machine on a button gesture.
	 *
	 * @param e the gesture; Event::None is ignored
	 */
	void on_button(button::Event e);

	/** When the state machine next needs to do something without the user.
	 *
	 * @return an absolute k_uptime_get() value: the countdown tick, the recalibration
	 *         itself, or the menu's idle timeout. INT64_MAX when nothing is pending.
	 */
	int64_t deadline_ms();

	/** Run whatever deadline_ms() was waiting for. Safe to call early; it re-checks. */
	void on_deadline();
}
