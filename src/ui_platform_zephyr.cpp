/* Zephyr implementation of the UI platform seam (see ui_platform.hpp). */
#include "ui_platform.hpp"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/display.h>

namespace {
const struct device *const disp_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
}

namespace plat {

bool display_ready()
{
	return device_is_ready(disp_dev);
}

void blanking_on()
{
	display_blanking_on(disp_dev);
}

void blanking_off()
{
	display_blanking_off(disp_dev);
}

void log(const char *msg)
{
	printk("%s", msg);
}

} // namespace plat
