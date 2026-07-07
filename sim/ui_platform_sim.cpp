/*
 * Host (preview) implementation of the UI platform seam (see ../src/ui/ui_platform.hpp).
 *
 * There is no panel on the PC, so blanking is a no-op and the display is always
 * "ready"; the sim captures LVGL's framebuffer directly (see sim.cpp). This is
 * the counterpart to src/ui/ui_platform.cpp (the Zephyr/board backend).
 */
#include "ui/ui_platform.hpp"

#include <cstdio>

namespace plat
{

    bool display_ready() { return true; }
    void blanking_on() {}
    void blanking_off() {}
    void log(const char *msg) { std::fputs(msg, stderr); }
    void register_render_pm() {} /* no panel on the PC */

} // namespace plat
