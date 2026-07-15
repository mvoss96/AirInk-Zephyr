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
	/** Name this build for the boot splash (default "Standalone"). Not copied; call before run(). */
	void set_build_name(const char *name);

	/** The last reading in centi-Celsius, for the offset editor's "Will read" prediction.
	 * INT32_MIN = no reading yet (0 is a temperature). */
	int32_t last_temp_x100();

	/** Drop the last reading. Called when the offset changes: the reading predates it, and no new
	 * one can arrive while the menu is open -- better "no reading yet" than a wrong prediction. */
	void forget_last_temp();

	/** Bring up display, sensor, battery and button, then loop forever. A missing display or button
	 * is survivable; a missing SCD41 parks the error view and blocks. */
	void run();
}
