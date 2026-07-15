#pragma once
#include <stdint.h>

#include "input/button.hpp"

/** @file
 * The settings menu.
 *
 * This module exists only while the user is inside it. It has no "sensor" state, knows nothing about
 * readings, the battery or the measurement cadence, and never refreshes the panel -- it only stages
 * its own views. The loop (app.cpp) owns the mode, the readings, and every other view.
 *
 *   enter()                      the user held the button on the sensor view
 *   proceed(gesture)  -> Running the user is still in here
 *                     -> Exited  put the readings back on screen
 *
 * WHAT the menu contains is not here and not in the display either: it is a table at the top of
 * menu.cpp, and that table is the whole definition. A row says what KIND it is -- a sub-menu, a
 * screen, a toggle, a number, a way out -- and the machinery knows how each kind behaves. Adding a
 * setting is adding a row.
 *
 * So this is the shape of it, which is the part that does not change when a row is added:
 *
 *   a list  --tap--------------> the next row that this build has
 *           --hold on Submenu--> that list          (Leave goes back to the parent)
 *           --hold on Toggle---> it flips, the row rewrites itself, nothing else moves
 *           --hold on Number---> the editor: tap steps and wraps, hold saves, idle discards
 *           --hold on Screen---> a flow of its own, below
 *           --hold on Leave----> the parent list, or out of the menu if this is the root
 *           --30 s idle--------> out of the menu
 *
 *   Screens, each of which ends up back in the list it was opened from:
 *
 *     CalibCo2  --hold--> CalibRun --3 min--> Recalibrated (leaves the menu: it is a completion)
 *        | tap / idle         | hold = abort        | the sensor said no
 *        v                    v                     v
 *      list                 list                  CalibFailed --any gesture / idle--> list
 *
 *     Matter       --any gesture / 2 min idle--> list
 *     FactoryReset --tap / 30 s idle-----------> list
 *          | hold = FactoryReset (net::factory_reset() reboots us)
 *
 * A failed calibration is the menu's own business: it puts the message up and waits for the user to
 * acknowledge it, so the loop never learns that one happened.
 */
namespace menu
{
	// Fresh outdoor air, the only concentration a user can reliably stand in.
	constexpr uint16_t CALIB_TARGET_PPM = 420;

	/** What proceed() wants the loop to do next. */
	enum class Status : uint8_t
	{
		Running,	  // still inside; nothing for the loop to do
		Exited,		  // show the readings again
		Recalibrated, // as Exited, but the retained CO2 value predates the correction
		FactoryReset  // the user confirmed it; the loop hands it to net, which can actually do it
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
