#pragma once
#include <stdint.h>
#include <stddef.h>

/** @file
 * Just enough <zephyr/kernel.h> for the app modules the host preview compiles (menu, prefs, net).
 *
 * The preview exists so the menu's own tables draw the mockups -- see sim/sim.cpp. Getting there
 * means compiling menu.cpp, and menu.cpp includes Zephyr. Nothing here emulates Zephyr: it declares
 * the three names those modules actually use, and sim/app_stubs.cpp answers them on the host.
 */

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif

/* button::queue()'s return type. The preview delivers gestures by calling menu::proceed()
 * directly, so a queue is never made -- only named. */
struct k_msgq;

#ifdef __cplusplus
extern "C"
{
#endif

	int printk(const char *fmt, ...);
	int64_t k_uptime_get(void);

#ifdef __cplusplus
}
#endif
