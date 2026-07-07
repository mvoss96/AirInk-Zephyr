#pragma once

/*
 * Thin platform seam for the LVGL UI. On the nRF target these forward to the
 * Zephyr display driver (ui_platform.cpp); in the host preview build the sim
 * provides its own no-op implementation (sim/ui_platform_sim.cpp). This lets the
 * exact same display_ui.cpp render both on the panel and on the PC.
 */
namespace plat
{

    bool display_ready();
    void blanking_on();
    void blanking_off();
    void log(const char *msg);

    /* Hook panel deep-sleep onto LVGL's render lifecycle (RESUME before a refresh,
     * SUSPEND after) to save battery. Currently a no-op: AirInk's vendored ssd16xx
     * driver has no pm_device support yet (unlike HappyPot's). Wiring is in place so
     * that only ui_platform.cpp changes once the driver gains PM. */
    void register_render_pm();

} // namespace plat
