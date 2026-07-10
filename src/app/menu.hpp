#pragma once
#include <stdint.h>

#include "input/button.hpp"

/** @file
 * The settings menu and the calibration flow.
 *
 * This module exists only while the user is inside it. It has no "sensor" state, knows
 * nothing about readings, the battery or the measurement cadence, and never refreshes
 * the panel -- it only stages its own three views. main.cpp owns the mode, the readings,
 * and every other view.
 *
 *   enter()                      the user held the button on the sensor view
 *   proceed(gesture)  -> Running the user is still in here
 *                     -> Exited  put the readings back on screen
 *                     -> ...     something happened that main has to render
 *
 * Root --hold(Calibrate)--> CalibPrompt --hold--> CalibRun --3 min--> Recalibrated
 *  | tap = next entry            | tap = Exited        | hold = Exited (aborts)
 *  | hold(Exit) / 30 s idle = Exited
 */
namespace menu
{
	// Fresh outdoor air, the only concentration a user can reliably stand in.
	constexpr uint16_t CALIB_TARGET_PPM = 420;

	/** What proceed() wants main to do next. */
	enum class Status : uint8_t
	{
		Running,      // still inside; nothing for main to do
		Exited,       // show the readings again
		Recalibrated, // the sensor was recalibrated; the retained CO2 value is stale
		Failed        // nothing was changed; the [CAL] log says whether the sensor
					  // refused the air it measured or did not answer at all
	};

	/** Open the menu on its first entry. */
	void enter();

	/** Advance the menu: one gesture and whatever its own clock has to say.
	 *
	 * Safe to call with Event::None -- that is how the countdown and the idle timeouts
	 * get their turn.
	 *
	 * @param e the gesture, or Event::None when only a deadline expired
	 * @return what main should render next; Running means the menu is still on screen
	 */
	Status proceed(button::Event e);

	/** When proceed() must run again without the user pressing anything.
	 *
	 * @return an absolute k_uptime_get() value: the next bar step, the recalibration
	 *         itself, or the idle timeout
	 */
	int64_t deadline_ms();

	/** Leave immediately, aborting a running calibration.
	 *
	 * For the low-battery warning, which outranks whatever the user was doing.
	 */
	void abort();
}
