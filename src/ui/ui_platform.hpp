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

    /* Panel deep-sleep bracket around one refresh, to cut idle current. The whole
     * refresh must sit between these — including blanking_off(), which is where the
     * ssd16xx actually triggers the full-refresh panel update; suspending earlier
     * (e.g. on LVGL's RENDER_READY) would sleep the panel before that update and
     * leave it blank. On nRF these run the ssd16xx pm_device actions (RESUME =
     * wake + reset + profile reload, SUSPEND = deep sleep mode 1); the pm subsystem
     * tracks state, so the redundant RESUME on the already-active boot render is a
     * no-op. In the host sim both are no-ops. */
    void display_resume();
    void display_suspend();

} // namespace plat
