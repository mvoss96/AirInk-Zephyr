#include <stdlib.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/sensor.h>
/* This header lacks an extern "C" guard, so wrap it to keep C linkage for
 * scd4x_persist_settings() etc. when compiled as C++. */
extern "C" {
#include <zephyr/drivers/sensor/scd4x.h>
}

#include "display_ui.hpp"

/* SCD41 CO2/temp/humidity sensor on I2C0 (SCL P0.22 / SDA P0.24), single-shot */
static const struct device *const scd41 = DEVICE_DT_GET(DT_NODELABEL(scd41));
static const struct i2c_dt_spec scd41_bus = I2C_DT_SPEC_GET(DT_NODELABEL(scd41));

/* Time between single-shot measurements. Each fetch itself blocks ~5 s, and a
 * full 4.2" e-paper refresh takes a few seconds, so keep this comfortably long. */
#define MEASURE_INTERVAL_MS 60000

/*
 * Wake the SCD41 (command 0x36F6). In single-shot mode the driver leaves it
 * powered down after init, and attr_set/attr_get do not wake it. NACKed while
 * asleep, so send it twice (same trick the driver uses internally).
 */
static void scd41_wake(void)
{
	const uint8_t cmd[2] = {0x36, 0xF6};

	i2c_write_dt(&scd41_bus, cmd, sizeof(cmd));
	i2c_write_dt(&scd41_bus, cmd, sizeof(cmd));
	k_msleep(30); /* wake-up time per datasheet */
}

/* Disable automatic self-calibration and persist to EEPROM (only writes EEPROM
 * when ASC is still enabled, to spare its limited endurance). */
static void scd41_disable_asc(void)
{
	struct sensor_value asc;

	scd41_wake();

	if (sensor_attr_get(scd41, SENSOR_CHAN_CO2,
			    (enum sensor_attribute)SENSOR_ATTR_SCD4X_AUTOMATIC_CALIB_ENABLE,
			    &asc) < 0) {
		printk("SCD41: could not read ASC state\n");
		return;
	}
	if (asc.val1 == 0) {
		printk("SCD41: ASC already disabled\n");
		return;
	}

	struct sensor_value off{}; /* val1 = val2 = 0 -> ASC off */

	if (sensor_attr_set(scd41, SENSOR_CHAN_CO2,
			    (enum sensor_attribute)SENSOR_ATTR_SCD4X_AUTOMATIC_CALIB_ENABLE,
			    &off) < 0) {
		printk("SCD41: failed to disable ASC\n");
		return;
	}
	if (scd4x_persist_settings(scd41) < 0) {
		printk("SCD41: failed to persist settings\n");
		return;
	}
	printk("SCD41: ASC disabled and persisted to EEPROM\n");
}

int main(void)
{
	const bool display_ok = (ui::init() == 0);

	printk("AirInk started (display %s)\n", display_ok ? "ok" : "FAILED");

	if (!device_is_ready(scd41)) {
		printk("SCD41 not ready\n");
		if (display_ok) {
			ui::show_error("SENSOR ERROR", "SCD41 not found");
		}
		return 0; /* leave the error on screen */
	}

	scd41_disable_asc();

	while (true) {
		if (sensor_sample_fetch(scd41) == 0) {
			struct sensor_value co2, temp, hum;

			sensor_channel_get(scd41, SENSOR_CHAN_CO2, &co2);
			sensor_channel_get(scd41, SENSOR_CHAN_AMBIENT_TEMP, &temp);
			sensor_channel_get(scd41, SENSOR_CHAN_HUMIDITY, &hum);

			const int32_t temp_x100 = temp.val1 * 100 + temp.val2 / 10000;
			const uint16_t hum_x100 = (uint16_t)(hum.val1 * 100 + hum.val2 / 10000);

			printk("CO2 %u ppm  T %d.%02d C  RH %d.%02d %%\n",
			       co2.val1,
			       temp.val1, abs(temp.val2) / 10000,
			       hum.val1, abs(hum.val2) / 10000);

			if (display_ok) {
				ui::show_reading((uint16_t)co2.val1, temp_x100, hum_x100);
			}
		} else {
			printk("SCD41: sample fetch failed\n");
			if (display_ok) {
				ui::show_error("SENSOR ERROR", "SCD41 read failed");
			}
		}

		k_msleep(MEASURE_INTERVAL_MS);
	}

	return 0;
}
