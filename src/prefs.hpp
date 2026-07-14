#pragma once
#include <stdint.h>

#include "sensors/scd41.hpp" // scd41::Trim -- the sensor owns what a trim is; we only keep the last one
#include "ui/display_ui.hpp" // ui::TempUnit -- the unit is a display preference, so it is declared there

/** @file
 * The settings the user owns, kept across a reboot.
 *
 * This is the device's only persistent store. Until it existed, nothing the user chose survived a
 * power cycle -- which is why the temperature unit could not be offered at all (see the note above
 * ui::List). The sensor's own trim could not either: the SCD41 does have an EEPROM, but it is good
 * for a few thousand writes, and these are settings a user sits and turns until the panel agrees with
 * the thermometer in their hand. So they live here, in NVS, and are written into the sensor's RAM on
 * every boot -- see scd41::set_trim().
 *
 * Backed by Zephyr settings over NVS in the "storage" partition (dts/airink_flash.dtsi: 0xEC000,
 * 32 KB) -- the same partition the Matter build keeps its fabrics in, under keys of our own. A write
 * happens only when the user changes something.
 *
 * The values are ints and bools and nothing else, and the store knows them by name. A new setting is
 * a new key and a new pair of accessors; there is no schema to migrate and no struct to version.
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

	/** What the SCD41 has been told about where it is standing. Defaults are scd41::Trim's.
	 *
	 * The struct is the sensor's, not a copy of it: there is one description of what a trim IS, and
	 * this is where the last one the user chose is kept.
	 */
	const scd41::Trim &trim();

	/** Change one part of the trim and write it down. The sensor is NOT told -- that is the caller's
	 * job (scd41::set_trim), because only the caller knows whether the sensor is busy measuring.
	 *
	 * @return 0, or a negative error from the store (the value is still changed)
	 */
	int set_temp_offset_x10(int tenths_c);
	int set_altitude_m(int metres);
	int set_auto_calib(bool on);
}
