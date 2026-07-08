#include "scd41.hpp"

#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/sensor.h>
/* This header lacks an extern "C" guard, so wrap it to keep C linkage for
 * scd4x_persist_settings() etc. when compiled as C++. */
extern "C"
{
#include <zephyr/drivers/sensor/scd4x.h>
}

namespace
{

	const struct device *const scd41_dev = DEVICE_DT_GET(DT_NODELABEL(scd41));
	const struct i2c_dt_spec scd41_bus = I2C_DT_SPEC_GET(DT_NODELABEL(scd41));

	/*
	 * Wake the SCD41 (command 0x36F6). In single-shot mode the driver leaves it
	 * powered down after init, and attr_set/attr_get do not wake it. NACKed while
	 * asleep, so send it twice (same trick the driver uses internally).
	 */
	void wake()
	{
		const uint8_t cmd[2] = {0x36, 0xF6};

		i2c_write_dt(&scd41_bus, cmd, sizeof(cmd));
		i2c_write_dt(&scd41_bus, cmd, sizeof(cmd));
		k_msleep(30); /* wake-up time per datasheet */
	}

	/* Disable automatic self-calibration and persist to EEPROM (only writes EEPROM
	 * when ASC is still enabled, to spare its limited endurance). */
	void disable_asc()
	{
		struct sensor_value asc;

		wake();

		if (sensor_attr_get(scd41_dev, SENSOR_CHAN_CO2,
							(enum sensor_attribute)SENSOR_ATTR_SCD4X_AUTOMATIC_CALIB_ENABLE,
							&asc) < 0)
		{
			printk("SCD41: could not read ASC state\n");
			return;
		}
		if (asc.val1 == 0)
		{
			printk("SCD41: ASC already disabled\n");
			return;
		}

		struct sensor_value off{}; /* val1 = val2 = 0 -> ASC off */

		if (sensor_attr_set(scd41_dev, SENSOR_CHAN_CO2,
							(enum sensor_attribute)SENSOR_ATTR_SCD4X_AUTOMATIC_CALIB_ENABLE,
							&off) < 0)
		{
			printk("SCD41: failed to disable ASC\n");
			return;
		}
		if (scd4x_persist_settings(scd41_dev) < 0)
		{
			printk("SCD41: failed to persist settings\n");
			return;
		}
		printk("SCD41: ASC disabled and persisted to EEPROM\n");
	}

} // namespace

int scd41::init()
{
	if (!device_is_ready(scd41_dev))
	{
		return -ENODEV;
	}
	disable_asc();
	return 0;
}

int scd41::sample(Scd41Reading *out)
{
	if (sensor_sample_fetch(scd41_dev) < 0)
	{
		return -EIO;
	}

	struct sensor_value co2, temp, hum;
	sensor_channel_get(scd41_dev, SENSOR_CHAN_CO2, &co2);
	sensor_channel_get(scd41_dev, SENSOR_CHAN_AMBIENT_TEMP, &temp);
	sensor_channel_get(scd41_dev, SENSOR_CHAN_HUMIDITY, &hum);

	out->co2_ppm = (uint16_t)co2.val1;
	out->temp_x100 = temp.val1 * 100 + temp.val2 / 10000;
	out->hum_x100 = (uint16_t)(hum.val1 * 100 + hum.val2 / 10000);
	return 0;
}
