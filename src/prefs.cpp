#include "prefs.hpp"

#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>

#include <errno.h>

namespace
{
	ui::TempUnit unit = ui::TempUnit::Celsius;
	bool store_up = false;

	/* Called by settings_load_subtree() once per key found under "airink". A key we do not know is
	 * not an error we should hide -- but it is not fatal either: it is what an older or newer
	 * firmware left behind. Return -ENOENT and the subsystem moves on. */
	int on_key(const char *name, size_t len, settings_read_cb read_cb, void *cb_arg)
	{
		if (!settings_name_steq(name, "temp_unit", nullptr))
		{
			return -ENOENT;
		}

		uint8_t v;
		if (len != sizeof(v))
		{
			return -EINVAL;
		}
		const ssize_t n = read_cb(cb_arg, &v, sizeof(v));
		if (n < 0)
		{
			return (int)n;
		}

		// Anything that is not Fahrenheit is Celsius. A stored byte we do not recognise (a rolled-back
		// firmware, a corrupted record) must not put the panel into a state that has no name.
		unit = (v == (uint8_t)ui::TempUnit::Fahrenheit) ? ui::TempUnit::Fahrenheit
														: ui::TempUnit::Celsius;
		return 0;
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

	store_up = true;
	printk("[PREFS] temp unit %s\n", unit == ui::TempUnit::Fahrenheit ? "F" : "C");
	return 0;
}

ui::TempUnit prefs::temp_unit()
{
	return unit;
}

int prefs::set_temp_unit(ui::TempUnit u)
{
	// The user's choice takes effect now, whatever the flash says. Refusing to switch the display
	// because the write failed would punish them twice for a fault that is not theirs.
	unit = u;

	if (!store_up)
	{
		return -ENODEV;
	}

	const uint8_t v = (uint8_t)u;
	const int err = settings_save_one("airink/temp_unit", &v, sizeof(v));
	if (err)
	{
		printk("[PREFS] temp unit %s (NOT saved: %d)\n", u == ui::TempUnit::Fahrenheit ? "F" : "C",
			   err);
		return err;
	}

	printk("[PREFS] temp unit %s (saved)\n", u == ui::TempUnit::Fahrenheit ? "F" : "C");
	return 0;
}
