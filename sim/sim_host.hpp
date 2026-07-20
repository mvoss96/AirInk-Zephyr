#pragma once
#include <stdint.h>

/** @file
 * The two pieces of device state the host preview gets to decide, shared by sim.cpp (which sets
 * them) and app_stubs.cpp (which answers the app's questions from them).
 */
namespace simhost
{
	/** The preview's only clock: LVGL's tick and k_uptime_get() both read this, so a menu that
	 * times out after 30 s and an animation agree on what a second was. */
	extern uint32_t tick_ms;

	/** What battery::charging() answers -- and therefore whether the menu's USB-only rows exist. */
	extern bool charging;

	/** Make the SCD41 refuse the next calibration, so the flow's failure screen can be walked to
	 * rather than painted by hand. */
	extern bool calib_fails;
}
