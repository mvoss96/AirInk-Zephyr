#pragma once

#include <stdint.h>

/** @file
 * The device: display, sensor, battery, button, menu, and the loop tying them together.
 *
 * A module, not a main(): the standalone firmware runs it as its only thread, the Matter build on a
 * thread of its own next to CHIP/OpenThread (apps/matter/src/app_task.cpp). Everything the loop says
 * to the world beyond the panel goes through net (net.hpp); a build that installs no hooks there is
 * the standalone one.
 *
 * The loop owns the panel: LVGL is not thread-safe, so nothing outside run()'s thread may call ui::.
 * Other threads leave values where the loop picks them up (see net::set_thread_connected()).
 */
namespace app
{
	/** Bring up display, sensor, battery and button, then loop forever. A missing display or button
	 * is survivable; a missing SCD41 parks the error view and blocks.
	 * @param build_name names this build on the splash and in the boot log -- the two firmwares
	 *                   look alike everywhere else. Not copied. */
	void run(const char *build_name = "Standalone");
}
