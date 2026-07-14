#pragma once
#include <stdint.h>

#include "ui/display_ui.hpp" // ui::TempUnit -- the unit is a display preference, so it is declared there

/** @file
 * The settings the user owns, kept across a reboot.
 *
 * This is the device's first persistent store. Until it existed, nothing the user chose survived a
 * power cycle -- which is why the temperature unit could not be offered at all (see the note above
 * ui::Menu). A calibration needs no store because it lives inside the SCD41; a unit has nowhere else
 * to live.
 *
 * Backed by Zephyr settings over NVS in the "storage" partition (dts/airink_flash.dtsi: 0xEC000,
 * 32 KB) -- the same partition the Matter build keeps its fabrics in, under a key of our own. A write
 * happens only when the user changes something, so the flash wear is measured in writes per lifetime.
 *
 * In the Matter build the same value is also mirrored into the Unit Localization cluster, so a
 * controller can read and set it. The mirror is a view; THIS is the store. Two stores would be two
 * truths, and they would drift.
 */
namespace prefs
{
	/** Load what was saved. Safe to call after the Matter stack has already initialised settings.
	 *
	 * A device with nothing saved yet -- factory-new, or freshly reset -- gets the defaults, which is
	 * not an error.
	 *
	 * @retval 0        the store is up; whatever was saved has been loaded
	 * @retval negative the store is unavailable; the getters still answer, with defaults, and a
	 *                  set_*() will report the failure rather than pretend it saved
	 */
	int init();

	/** The unit the panel should show. Celsius until someone says otherwise. */
	ui::TempUnit temp_unit();

	/** Change the unit and write it down.
	 *
	 * The value takes effect for temp_unit() even if the write fails -- the user asked for it, and a
	 * flash that will not take it is not a reason to also ignore the request for this session.
	 *
	 * @return 0, or a negative error from the store (the value is still changed)
	 */
	int set_temp_unit(ui::TempUnit u);
}
