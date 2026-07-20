#include "sim_host.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstring>

#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>

#include "sensors/battery.hpp"
#include "sensors/scd41.hpp"

/** @file
 * What the app modules stand on, on a PC.
 *
 * The preview compiles menu.cpp, prefs.cpp and net.cpp as they are -- that is the point of it, so
 * the mockups come from the real tables. Those three reach for a kernel, a settings store, a sensor
 * and a battery; here is where each of those reaching-out ends. Anything a mockup should be able to
 * SHOW (ranges, labels, row visibility) is deliberately NOT decided here: it is decided in src/.
 */

namespace simhost
{
	uint32_t tick_ms = 0;
	bool charging = false;
	bool calib_fails = false;
}

int printk(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	const int n = std::vprintf(fmt, ap);
	va_end(ap);
	return n;
}

int64_t k_uptime_get(void)
{
	return simhost::tick_ms;
}

/* No store on a PC, and prefs.cpp is written for exactly that: init() succeeding with nothing to
 * load is a device booting on factory defaults, which is the right thing for a mockup to draw. */
int settings_subsys_init(void)
{
	return 0;
}

int settings_register(struct settings_handler *)
{
	return 0;
}

int settings_load_subtree(const char *)
{
	return 0;
}

int settings_save_one(const char *, const void *, size_t)
{
	return 0;
}

int settings_name_steq(const char *name, const char *key, const char **next)
{
	if (next)
	{
		*next = nullptr;
	}
	return std::strcmp(name, key) == 0;
}

/* The sensor, agreeable. The calibration flow is a sequence of SCREENS, and every screen in it is
 * on the far side of a call that succeeded -- a refusing sensor would render one of them and stop. */
int scd41::set_trim(const Trim &)
{
	return 0;
}

int scd41::calibrate_begin()
{
	return simhost::calib_fails ? -5 : 0;
}

int scd41::calibrate_finish(uint16_t, int16_t *correction_ppm)
{
	if (correction_ppm)
	{
		*correction_ppm = -12; // a plausible nudge, so the result screen has a number to show
	}
	return 0;
}

int scd41::calibrate_abort()
{
	return 0;
}

bool battery::charging()
{
	return simhost::charging;
}
