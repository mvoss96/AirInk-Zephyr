#pragma once
#include <stdint.h>

#include "input/button.hpp"

/** @file
 * The settings menu. Exists only while the user is inside it: knows nothing about readings or the
 * cadence, never refreshes the panel, only stages its own views. The loop (app.cpp) owns the rest.
 *
 * WHAT the menu contains is a table at the top of menu.cpp -- a row says what KIND it is (sub-menu,
 * screen, toggle, number, way out) and the machinery knows how each kind behaves. Adding a setting
 * is adding a row.
 *
 * The shape: tap = next row, hold = activate, 30 s idle = out. A Toggle flips in place; a Number
 * opens the one editor (tap steps and wraps, hold saves, idle discards); a Screen runs its own
 * little flow and ends up back in the list -- except a completed calibration, a confirmed factory
 * reset, and the Matter QR (scanned or dismissed), which leave the menu via Status below.
 */
namespace menu
{
	/** What proceed() wants the loop to do next. */
	enum class Status : uint8_t
	{
		Running,	  // still inside
		Exited,		  // show the readings again
		Recalibrated, // as Exited, but the retained CO2 value predates the correction
		FactoryReset  // confirmed; the loop hands it to net::factory_reset()
	};

	/** Open the menu on its first entry. */
	void enter();

	/** Open the menu directly on the Matter screen -- the boot onboarding of a device that has
	 * never been commissioned. From there it behaves like any menu: the loop drives it with
	 * proceed(), and it exits (scanned, or dismissed) to the readings. */
	void enter_matter();

	/** Advance the menu: one gesture, plus whatever its own clock says. Safe with Event::None --
	 * that is how countdowns and idle timeouts get their turn. */
	Status proceed(button::Event e);

	/** When proceed() must run again unprompted (bar step, calibration end, idle timeout).
	 * @return absolute k_uptime_get() deadline */
	int64_t deadline_ms();

	/** Leave immediately, aborting a running calibration. For the low-battery warning. */
	void abort();
}
