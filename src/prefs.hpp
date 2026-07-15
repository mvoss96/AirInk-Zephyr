#pragma once
#include <stdint.h>

#include "sensors/scd41.hpp" // scd41::Trim -- the sensor owns what a trim is
#include "ui/display_ui.hpp" // ui::TempUnit -- the unit is a display preference

/** @file
 * The settings the user owns: kept across a reboot, and put where they have to act.
 *
 * The device's only persistent store, Zephyr settings over NVS in the "storage" partition -- the
 * same partition the Matter build keeps its fabrics in, under keys of our own. The sensor trim
 * lives here and NOT in the SCD41's EEPROM (good for a few thousand writes, and these are values
 * a user sits and turns); it is pushed into the sensor's RAM on every boot.
 *
 * One setter, not five: a setting has several homes (flash, panel, sensor, network), and keeping
 * them in step used to be each caller's hand-written ritual -- forgettable, silently. set() clamps,
 * saves and applies; a new setting is a row in prefs.cpp's table, enforced by static_assert.
 */
namespace prefs
{
	/** Every setting, in the table's order (prefs.cpp). */
	enum Id : uint8_t
	{
		Unit,		// ui::TempUnit
		TempOffset, // tenths of a degree C, subtracted by the sensor
		Altitude,	// metres above sea level
		AutoCalib,	// the SCD41's self-calibration, 0/1
		COUNT,
	};

	/** Load what was saved -- but do not act on it yet (the panel and sensor are not up).
	 * A missing store is survivable: getters answer defaults, set() logs instead of saving.
	 * @retval 0 loaded; negative = store unavailable */
	int init();

	/** Put every setting where it acts: panel, sensor, network. Once, after both are up. */
	void apply_all();

	/** Current value in stored units (bool = 0/1, unit = ui::TempUnit's value). */
	int32_t get(Id id);

	/** Valid range. The store clamps to it and the menu's editor offers exactly it. */
	int32_t lo(Id id);
	int32_t hi(Id id);

	/** The user chose this on the panel: clamp, save, apply -- including telling the network.
	 * Takes effect even if the flash write fails. A value that did not move costs nothing. */
	void set(Id id, int32_t v);

	/** The network chose this: same as set(), minus telling the network its own news. */
	void adopt(Id id, int32_t v);
}
