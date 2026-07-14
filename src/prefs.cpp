#include "prefs.hpp"

#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>

#include <errno.h>
#include <string.h>

namespace
{
	ui::TempUnit unit = ui::TempUnit::Celsius;
	scd41::Trim sensor_trim{};
	bool store_up = false;

	/* Every setting, once: its key, where it lives, and how wide it is on the wire.
	 *
	 * The point of the table is that this is the ONLY place a setting is named. Loading, saving and
	 * defaulting all walk it, so adding one is adding a row -- not a new case in a switch, a new
	 * branch in the loader, and a new save function that looks like the last three. */
	struct Field
	{
		const char *key;
		void *value;   // int32_t* or uint8_t*, per `bytes`
		size_t bytes;  // 4 for an int, 1 for a bool or an enum
	};

	/* NB: the addresses are of the module state above, so the table is const and the values are not. */
	const Field FIELDS[] = {
		{"temp_unit", &unit, 1},
		{"temp_offset", &sensor_trim.temp_offset_x10, sizeof(sensor_trim.temp_offset_x10)},
		{"altitude", &sensor_trim.altitude_m, sizeof(sensor_trim.altitude_m)},
		{"auto_calib", &sensor_trim.auto_calib, 1},
	};

	const Field *field_for(const char *name)
	{
		for (const Field &f : FIELDS)
		{
			if (settings_name_steq(name, f.key, nullptr))
			{
				return &f;
			}
		}
		return nullptr;
	}

	/* Called by settings_load_subtree() once per key found under "airink". A key we do not know is
	 * not an error we should hide -- but it is not fatal either: it is what an older or newer
	 * firmware left behind. Return -ENOENT and the subsystem moves on. */
	int on_key(const char *name, size_t len, settings_read_cb read_cb, void *cb_arg)
	{
		const Field *f = field_for(name);
		if (!f)
		{
			return -ENOENT;
		}
		if (len != f->bytes)
		{
			return -EINVAL; // a record of the wrong width is a record from another firmware
		}
		const ssize_t n = read_cb(cb_arg, f->value, f->bytes);
		return (n < 0) ? (int)n : 0;
	}

	int save(const Field &f)
	{
		if (!store_up)
		{
			return -ENODEV;
		}
		char key[32];
		snprintf(key, sizeof(key), "airink/%s", f.key);
		const int err = settings_save_one(key, f.value, f.bytes);
		if (err)
		{
			printk("[PREFS] %s NOT saved (%d)\n", f.key, err);
		}
		return err;
	}

	settings_handler handler = {
		.name = "airink",
		.h_get = nullptr,
		.h_set = on_key,
		.h_commit = nullptr,
		.h_export = nullptr,
	};

	/* Nothing stops a corrupt record, or one from a firmware that allowed more, from arriving out of
	 * range -- and an out-of-range value here is one the SCD41 will reject, silently, for the life of
	 * the device. Clamp on the way in, once. */
	int clamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
} // namespace

int prefs::init()
{
	// Idempotent, and it has to be: in the Matter build the CHIP stack has already brought the
	// settings subsystem up for its fabrics by the time we get here, and in the standalone build
	// nobody has. Zephyr returns 0 on the second call rather than failing, so both roads work.
	int err = settings_subsys_init();
	if (err)
	{
		printk("Prefs: settings unavailable (%d); choices will not survive a reboot\n", err);
		return err;
	}

	err = settings_register(&handler);
	if (err && err != -EEXIST)
	{
		printk("Prefs: could not register (%d)\n", err);
		return err;
	}

	err = settings_load_subtree("airink");
	if (err)
	{
		printk("Prefs: could not load (%d)\n", err);
		return err;
	}

	// Anything that is not Fahrenheit is Celsius: a stored byte we do not recognise must not put the
	// panel into a state that has no name.
	if (unit != ui::TempUnit::Fahrenheit)
	{
		unit = ui::TempUnit::Celsius;
	}
	sensor_trim.temp_offset_x10 = clamp(sensor_trim.temp_offset_x10, 0, 200);
	sensor_trim.altitude_m = clamp(sensor_trim.altitude_m, 0, 3000);

	store_up = true;
	printk("[PREFS] unit %s, offset %d.%d C, altitude %d m, self-calib %s\n",
		   unit == ui::TempUnit::Fahrenheit ? "F" : "C", sensor_trim.temp_offset_x10 / 10,
		   sensor_trim.temp_offset_x10 % 10, sensor_trim.altitude_m,
		   sensor_trim.auto_calib ? "on" : "off");
	return 0;
}

ui::TempUnit prefs::temp_unit()
{
	return unit;
}

const scd41::Trim &prefs::trim()
{
	return sensor_trim;
}

int prefs::set_temp_unit(ui::TempUnit u)
{
	// The user's choice takes effect now, whatever the flash says. Refusing to switch the display
	// because the write failed would punish them twice for a fault that is not theirs.
	unit = u;
	return save(FIELDS[0]);
}

int prefs::set_temp_offset_x10(int tenths_c)
{
	sensor_trim.temp_offset_x10 = clamp(tenths_c, 0, 200);
	return save(FIELDS[1]);
}

int prefs::set_altitude_m(int metres)
{
	sensor_trim.altitude_m = clamp(metres, 0, 3000);
	return save(FIELDS[2]);
}

int prefs::set_auto_calib(bool on)
{
	sensor_trim.auto_calib = on;
	return save(FIELDS[3]);
}
