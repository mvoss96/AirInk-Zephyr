#pragma once
#include <stdint.h>

/*
 * LVGL UI for the AirInk 4.2" 400x300 e-paper.
 * one flat set of widgets on the active screen, one view visible at a time, plus a persistent
 * status bar (battery + link) that is never hidden. See display_ui.cpp.
 */
namespace ui
{

    /* Connectivity shown in the status bar. */
    enum class Link
    {
        None,
        BleAdv,
        BleConnected,
        ZigbeeJoining,
        ZigbeeConnected
        /* , Thread… */
    };

    /* Build all widgets and show the boot splash. Returns 0 on success. */
    int init();

    /* Views — one function per view (no enum navigation needed). */
    void show_reading(uint16_t co2_ppm, int32_t temp_c_x100, uint16_t hum_x100);
    void show_error(const char *title, const char *detail);
    /* Dedicated full-screen low-battery warning (the app decides the threshold). */
    void show_low_battery(uint8_t percent);
    /* Factory-reset countdown. The app calls this each tick with the seconds left
     * while the reset button is held; at 0 it performs the reset, on release it
     * returns to a normal view. Entry is a full refresh, ticks are partial. */
    void show_reset(uint8_t seconds_left);

    /* Status-bar data — visible on every view, decoupled from the radio/gauge stack. */
    void set_battery(uint8_t percent, bool charging = false);
    void set_link(Link state);

} // namespace ui
