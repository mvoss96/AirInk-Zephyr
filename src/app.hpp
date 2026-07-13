#pragma once

#include "sensors/battery.hpp"
#include "sensors/scd41.hpp"
#include "ui/display_ui.hpp"

/** @file
 * The AirInk device itself: display, sensor, battery, button, menu, and the loop that
 * ties them together. Everything the product does when nobody is holding the button.
 *
 * This is a module rather than a main() because it has two callers. The standalone
 * firmware runs it as its only thread (apps/standalone/main.cpp). The Matter build runs it on a
 * thread of its own, alongside the CHIP event loop and OpenThread, and taps the hooks
 * below to publish the same numbers over Thread (apps/matter/src/app_task.cpp).
 *
 * The loop owns the panel: LVGL is not thread-safe, and nothing outside run()'s thread
 * may call ui::. A caller that wants to change what is on screen -- the Matter build
 * does, for the link indicator -- leaves the value somewhere the loop can pick it up.
 */
namespace app
{
	/** Observers, called on run()'s thread once per cycle. Either may be null.
	 *
	 * They exist so a caller can forward the readings somewhere else without the loop
	 * learning what that somewhere is. Both are called from run()'s thread, so a hook
	 * that blocks stalls the measurement cadence and the panel with it.
	 */
	struct Hooks
	{
		/** A fresh reading. Not called on a read error, and not called on the nine of
		 * ten cycles that skip the CO2 read -- those carry the retained value, and
		 * republishing it would be a lie about its age. */
		void (*reading)(const Scd41Reading &r);

		/** The battery, every cycle, whatever else happened. */
		void (*battery)(const battery::State &bat);
	};

	/** Install the observers. Call before run(). */
	void set_hooks(const Hooks &hooks);

	/** Report the radio state for the status bar, from any thread.
	 *
	 * Only records the value -- the panel belongs to run()'s thread, which puts it up on
	 * its next cycle. So the indicator can lag a connection by up to one cycle, which is
	 * the same latency an e-paper refresh imposes anyway.
	 */
	void set_link(ui::Link state);

	/** Bring up the display, sensors and button, then loop over measurement cycles.
	 *
	 * Never returns. A missing display or button is survivable and only logged; a
	 * missing SCD41 is not -- there is nothing to show -- so that one parks the error
	 * view on screen and blocks forever.
	 */
	void run();
}
