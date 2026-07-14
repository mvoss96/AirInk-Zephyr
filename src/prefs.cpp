#include "prefs.hpp"

#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "net.hpp" // net::publish_unit -- the one setting the network keeps a copy of

namespace
{
	ui::TempUnit unit = ui::TempUnit::Celsius;
	scd41::Trim sensor_trim{};
	bool store_up = false;

	/* What has to happen once a setting has changed, whoever changed it.
	 *
	 * @param local true when the user chose it here, false when it came in from the network -- the
	 *              only difference is whether the network is told, and telling it what it just told
	 *              us would be an echo, not a sync.
	 */
	void apply_unit(bool local)
	{
		ui::set_temp_unit(unit);
		if (local)
		{
			net::publish_unit(unit); // no-op in a build with no network
		}
	}

	/* Three of the four settings are the sensor's trim, and the sensor takes it in one go. */
	void apply_trim(bool)
	{
		scd41::set_trim(sensor_trim);
	}

	/* Every setting, once: its key, where it lives, how wide it is on the wire, what a valid value is,
	 * and what has to happen when it changes.
	 *
	 * The point of the table is that this is the ONLY place a setting is described. Loading, saving,
	 * clamping, defaulting and applying all walk it, so adding one is adding a row -- not a new case in
	 * the loader, a new setter that looks like the last three, and a new line in somebody else's
	 * aftermath that nothing would have missed. */
	struct Setting
	{
		const char *key;
		void *value;			  // int32_t* or uint8_t*, per `bytes`
		size_t bytes;			  // 4 for an int, 1 for a bool or an enum
		int32_t lo, hi;			  // what a valid value is; the menu's editor offers exactly this
		void (*apply)(bool local);
	};

	/* In prefs::Id order, and that order is the contract: C++ has no designated initialisers for
	 * arrays, so the static_assert below can catch a missing row but not a swapped one. */
	const Setting SETTINGS[] = {
		/* Unit       */ {"temp_unit", &unit, 1, 0, 1, apply_unit},
		/* TempOffset */ {"temp_offset", &sensor_trim.temp_offset_x10,
						  sizeof(sensor_trim.temp_offset_x10), 0, 200, apply_trim},
		/* Altitude   */ {"altitude", &sensor_trim.altitude_m, sizeof(sensor_trim.altitude_m), 0, 3000,
						  apply_trim},
		/* AutoCalib  */ {"auto_calib", &sensor_trim.auto_calib, 1, 0, 1, apply_trim},
	};
	static_assert(ARRAY_SIZE(SETTINGS) == (size_t)prefs::COUNT, "a setting with no row");

	/* The values are ints, bools and one enum, so the table stores an address and a width and reads
	 * through it. Narrow enough that a switch would be three cases of the same thing. */
	int32_t read(const Setting &s)
	{
		return (s.bytes == 1) ? (int32_t) * (const uint8_t *)s.value : *(const int32_t *)s.value;
	}

	void write(const Setting &s, int32_t v)
	{
		if (s.bytes == 1)
		{
			*(uint8_t *)s.value = (uint8_t)v;
		}
		else
		{
			*(int32_t *)s.value = v;
		}
	}

	int32_t clamp(const Setting &s, int32_t v)
	{
		return v < s.lo ? s.lo : (v > s.hi ? s.hi : v);
	}

	const Setting *field_for(const char *name)
	{
		for (const Setting &s : SETTINGS)
		{
			if (settings_name_steq(name, s.key, nullptr))
			{
				return &s;
			}
		}
		return nullptr;
	}

	/* Called by settings_load_subtree() once per key found under "airink". A key we do not know is not
	 * an error we should hide -- but it is not fatal either: it is what an older or newer firmware left
	 * behind. Return -ENOENT and the subsystem moves on. */
	int on_key(const char *name, size_t len, settings_read_cb read_cb, void *cb_arg)
	{
		const Setting *s = field_for(name);
		if (!s)
		{
			return -ENOENT;
		}
		if (len != s->bytes)
		{
			return -EINVAL; // a record of the wrong width is a record from another firmware
		}
		const ssize_t n = read_cb(cb_arg, s->value, s->bytes);
		return (n < 0) ? (int)n : 0;
	}

	int save(const Setting &s)
	{
		if (!store_up)
		{
			return -ENODEV;
		}
		char key[32];
		snprintf(key, sizeof(key), "airink/%s", s.key);
		const int err = settings_save_one(key, s.value, s.bytes);
		if (err)
		{
			printk("[PREFS] %s NOT saved (%d)\n", s.key, err);
		}
		return err;
	}

	/* The one road in. Everything else is a caller deciding whether the network hears about it. */
	void change(prefs::Id id, int32_t v, bool local)
	{
		const Setting &s = SETTINGS[id];

		v = clamp(s, v);
		if (v == read(s))
		{
			return; // nothing moved: no flash write, no I2C burst, no radio
		}

		write(s, v);
		save(s);
		s.apply(local);
	}

	settings_handler handler = {
		.name = "airink",
		.h_get = nullptr,
		.h_set = on_key,
		.h_commit = nullptr,
		.h_export = nullptr,
	};
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

	// Nothing stops a corrupt record, or one from a firmware that allowed more, from arriving out of
	// range -- and an out-of-range value here is one the SCD41 will reject, silently, for the life of
	// the device. Clamp on the way in, once, from the same bounds the setters use.
	for (const Setting &s : SETTINGS)
	{
		write(s, clamp(s, read(s)));
	}

	store_up = true;
	printk("[PREFS] unit %s, offset %d.%d C, altitude %d m, self-calib %s\n",
		   unit == ui::TempUnit::Fahrenheit ? "F" : "C", sensor_trim.temp_offset_x10 / 10,
		   sensor_trim.temp_offset_x10 % 10, sensor_trim.altitude_m,
		   sensor_trim.auto_calib ? "on" : "off");
	return 0;
}

void prefs::apply_all()
{
	/* Walked rather than spelled out, so a new row cannot be forgotten here -- that forgetting is the
	 * whole reason this file has a table. But an apply shared by several rows runs ONCE: three of the
	 * four settings are the sensor's trim, and scd41::set_trim() is a wake, three I2C writes and a
	 * power-down, not a variable assignment. */
	void (*done[COUNT])(bool) = {};
	size_t n = 0;

	for (const Setting &s : SETTINGS)
	{
		bool already = false;
		for (size_t i = 0; i < n; i++)
		{
			already = already || (done[i] == s.apply);
		}
		if (already)
		{
			continue;
		}
		done[n++] = s.apply;

		// `true`: the network has not spoken yet, and its own copy of the unit is one it reloaded on
		// boot. Pushing outward here is what settles who is in charge -- prefs is the authority, the
		// cluster is the mirror. If we only ever listened, the controller's copy would win every
		// restart and the unit the user set on the panel would not survive one.
		s.apply(true);
	}
}

int32_t prefs::get(Id id)
{
	return read(SETTINGS[id]);
}

int32_t prefs::lo(Id id)
{
	return SETTINGS[id].lo;
}

int32_t prefs::hi(Id id)
{
	return SETTINGS[id].hi;
}

void prefs::set(Id id, int32_t v)
{
	change(id, v, true);
}

void prefs::adopt(Id id, int32_t v)
{
	change(id, v, false);
}
