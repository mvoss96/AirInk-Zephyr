/* Zephyr implementation of the UI platform seam (see ui_platform.hpp). */
#include "ui_platform.hpp"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/display.h>

namespace
{
	const struct device *const disp_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
}

namespace plat
{

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

	void register_render_pm()
	{
		/* No-op until the vendored ssd16xx driver implements pm_device actions.
		 * Once it does (HappyPot-style: SUSPEND -> deep sleep mode 1, RESUME -> HW
		 * reset + profile reload) plus CONFIG_PM_DEVICE=y, wire the panel deep-sleep
		 * here:
		 *   lv_display_t *d = lv_display_get_default();
		 *   lv_display_add_event_cb(d, on_render_start, LV_EVENT_RENDER_START, NULL);
		 *   lv_display_add_event_cb(d, on_render_ready, LV_EVENT_RENDER_READY, NULL);
		 * with on_render_* calling pm_device_action_run(disp_dev, RESUME/SUSPEND). */
	}

} // namespace plat
