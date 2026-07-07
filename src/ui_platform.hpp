#pragma once

/*
 * Thin platform seam for the LVGL UI. On the nRF target these forward to the
 * Zephyr display driver (ui_platform_zephyr.cpp); in the host preview build the
 * sim provides its own no-op implementation (sim/ui_platform_sim.cpp). This lets
 * the exact same display_ui.cpp render both on the panel and on the PC.
 */
namespace plat {

bool display_ready();
void blanking_on();
void blanking_off();
void log(const char *msg);

} // namespace plat
