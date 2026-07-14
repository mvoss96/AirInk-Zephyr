#pragma once
#include <stdint.h>

#include "sensors/scd41.hpp" // scd41::Trim -- the sensor owns what a trim is; we only keep the last one
#include "ui/display_ui.hpp" // ui::TempUnit -- the unit is a display preference, so it is declared there

/** @file
 * The settings the user owns: kept across a reboot, and put where they have to act.
 *
 * This is the device's only persistent store. Until it existed, nothing the user chose survived a
 * power cycle -- which is why the temperature unit could not be offered at all. The sensor's own trim
 * could not either: the SCD41 does have an EEPROM, but it is good for a few thousand writes, and these
 * are settings a user sits and turns until the panel agrees with the thermometer in their hand. So they
 * live here, in NVS, and are written into the sensor's RAM on every boot -- see scd41::set_trim().
 *
 * Backed by Zephyr settings over NVS in the "storage" partition (dts/airink_flash.dtsi: 0xEC000,
 * 32 KB) -- the same partition the Matter build keeps its fabrics in, under keys of our own.
 *
 * ---- Why there is one setter and not five ----
 *
 * A setting here has more than one home. The unit lives in flash AND on the panel AND (on a radio
 * build) in a Matter cluster; the trim lives in flash AND in the sensor's RAM. Keeping those in step
 * used to be the caller's job, and every caller did it slightly differently: four setters in menu.cpp,
 * each with its own hand-written aftermath, and a push_trim() that the fifth setting would have been
 * free to forget. Nothing would have complained -- the device would simply have stored the altitude
 * and never told the sensor, until the next reboot quietly fixed it.
 *
 * So the aftermath belongs to the setting, not to whoever changed it. set() clamps the value, writes
 * it down, and applies it -- and a new setting is a row in one table (prefs.cpp), not a new function
 * here plus a new ritual there.
 */
namespace prefs
{
	/** Every setting the user owns. The order is the table's order in prefs.cpp. */
	enum Id : uint8_t
	{
		Unit,		// ui::TempUnit
		TempOffset, // tenths of a degree C, subtracted by the sensor
		Altitude,	// metres above sea level
		AutoCalib,	// the SCD41's self-calibration, 0/1
		COUNT,
	};

	/** Load what was saved, but do NOT act on it yet -- see apply_all().
	 *
	 * A device with nothing saved -- factory-new, or freshly reset -- gets the defaults, which is not
	 * an error. Neither is a store that will not come up: the getters still answer, and a set() will
	 * report the failure rather than pretend it saved.
	 *
	 * @retval 0        the store is up; whatever was saved has been loaded
	 * @retval negative the store is unavailable
	 */
	int init();

	/** Put every setting where it has to act: the panel, the sensor, the network.
	 *
	 * Separate from init() because the things a setting acts ON are not up yet when the settings are
	 * read -- the display and the SCD41 are brought up after. Call once, after both.
	 */
	void apply_all();

	/** What a setting is now, in its stored units. A bool is 0 or 1; the unit is ui::TempUnit's value. */
	int32_t get(Id id);

	/** What a valid value is. The store clamps to this, and the menu's editor offers exactly it -- a
	 * range the user can dial in but the store would refuse is a promise the panel cannot keep. */
	int32_t lo(Id id);
	int32_t hi(Id id);

	/** The user chose this, here, on the panel.
	 *
	 * Clamps it, writes it down, and applies it -- which for the unit includes telling the network.
	 * A value that did not actually move costs nothing: no flash write, no I2C, no radio.
	 *
	 * The value takes effect even if the write to flash fails. The user asked for it, and a store that
	 * will not take it is not a reason to also ignore them for the rest of the session.
	 */
	void set(Id id, int32_t v);

	/** The network chose this, and we are adopting it.
	 *
	 * Identical to set(), minus the one thing that would be absurd: telling the network what it has
	 * just told us.
	 */
	void adopt(Id id, int32_t v);
}
