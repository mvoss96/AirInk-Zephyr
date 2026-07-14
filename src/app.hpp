#pragma once

#include <stdint.h>

/** @file
 * The AirInk device itself: display, sensor, battery, button, menu, and the loop that
 * ties them together. Everything the product does when nobody is holding the button.
 *
 * This is a module rather than a main() because it has two callers. The standalone
 * firmware runs it as its only thread (apps/standalone/main.cpp). The Matter build runs it on a
 * thread of its own, alongside the CHIP event loop and OpenThread (apps/matter/src/app_task.cpp).
 *
 * Everything the loop says to the world beyond the panel goes through net (net.hpp): the network
 * build installs hooks there, and a build that installs none is the standalone one. The loop itself
 * has no idea which it is running in -- that is the point.
 *
 * The loop owns the panel: LVGL is not thread-safe, and nothing outside run()'s thread
 * may call ui::. A caller that wants to change what is on screen leaves the value where the
 * loop can pick it up (see net::set_thread_connected()).
 */
namespace app
{
	/** Name this build, for the boot splash. Call before run().
	 *
	 * Two builds of this firmware exist and they look alike everywhere else, so the panel says
	 * which one is on the board. A caller that says nothing gets "Standalone", which is what a
	 * build that installs no hooks and no codes is.
	 *
	 * The string is not copied. It must outlive run(), which never returns.
	 */
	void set_build_name(const char *name);

	/** The last temperature the sensor gave us, in hundredths of a degree Celsius.
	 *
	 * For the temperature-offset editor, which predicts what the panel will read once the offset the
	 * user is turning takes effect -- so they can tap until it agrees with the thermometer in their
	 * hand instead of doing the arithmetic themselves.
	 *
	 * @return the reading, or INT32_MIN if the sensor has not answered yet
	 */
	int32_t last_temp_x100();

	/** Throw the last reading away, because it no longer describes anything.
	 *
	 * Called when the temperature offset changes: the reading was taken under the old one, and no new
	 * one can arrive while the menu is open. Predicting against it would quote a number that is
	 * already wrong. Better to admit there is nothing to predict from.
	 */
	void forget_last_temp();

	/** Bring up the display, sensors and button, then loop over measurement cycles.
	 *
	 * Never returns. A missing display or button is survivable and only logged; a
	 * missing SCD41 is not -- there is nothing to show -- so that one parks the error
	 * view on screen and blocks forever.
	 */
	void run();
}
