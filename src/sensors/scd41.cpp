#include "scd41.hpp"
#include "../util/ema.hpp"

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
	Ema<1> temp_ema; // Lightest filter: a real step is ~90 % shown after ~3 ticks.

	/** Wake the SCD41 (command 0x36F6) and wait out its wake-up time.
	 * In single-shot mode the driver leaves the sensor powered down after init, and
	 * attr_set/attr_get do not wake it. The command is NACKed while asleep, so send
	 * it twice (the same trick the driver uses internally).
	 */
	void wake()
	{
		const uint8_t cmd[2] = {0x36, 0xF6};

		i2c_write_dt(&scd41_bus, cmd, sizeof(cmd));
		i2c_write_dt(&scd41_bus, cmd, sizeof(cmd));
		k_msleep(30); // wake-up time per datasheet
	}

	/** Disable automatic self-calibration and persist the setting to EEPROM.
	 * Writes EEPROM only when ASC is still enabled, to spare its limited endurance.
	 * Failures are logged, not propagated: a sensor with ASC still on still measures.
	 */
	void disable_asc()
	{
		struct sensor_value asc;

		wake();

		if (sensor_attr_get(scd41_dev, SENSOR_CHAN_CO2, (enum sensor_attribute)SENSOR_ATTR_SCD4X_AUTOMATIC_CALIB_ENABLE, &asc) < 0)
		{
			printk("SCD41: could not read ASC state\n");
			return;
		}
		if (asc.val1 == 0)
		{
			printk("SCD41: ASC already disabled\n");
			return;
		}

		struct sensor_value off{}; // val1 = val2 = 0 -> ASC off

		if (sensor_attr_set(scd41_dev, SENSOR_CHAN_CO2, (enum sensor_attribute)SENSOR_ATTR_SCD4X_AUTOMATIC_CALIB_ENABLE, &off) < 0)
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
		return -EIO; // failed to fetch sample
	}

	struct sensor_value co2, temp, hum;
	sensor_channel_get(scd41_dev, SENSOR_CHAN_CO2, &co2);
	sensor_channel_get(scd41_dev, SENSOR_CHAN_AMBIENT_TEMP, &temp);
	sensor_channel_get(scd41_dev, SENSOR_CHAN_HUMIDITY, &hum);

	out->co2_ppm = (uint16_t)co2.val1;
	out->temp_x100 = temp_ema.update(temp.val1 * 100 + temp.val2 / 10000);
	out->hum_x100 = (uint16_t)(hum.val1 * 100 + hum.val2 / 10000);
	return 0;
}

int scd41::sample_rht(Scd41Reading *out)
{
	// Skip the CO2 photoacoustic pulse: measure_single_shot_rht_only (0x2196)
	// returns T+RH in ~50 ms at ~1000x less energy than a full read. Raw I2C
	// because the Zephyr driver only exposes the full single-shot.
	wake();

	const uint8_t meas[2] = {0x21, 0x96};
	if (i2c_write_dt(&scd41_bus, meas, sizeof(meas)) < 0)
	{
		return -EIO;
	}
	k_msleep(60); // rht-only measurement time (< 50 ms per datasheet)

	const uint8_t rd_cmd[2] = {0xEC, 0x05}; // read_measurement
	uint8_t rd[9];							// CO2+crc, T+crc, RH+crc
	if (i2c_write_dt(&scd41_bus, rd_cmd, sizeof(rd_cmd)) < 0 ||
		i2c_read_dt(&scd41_bus, rd, sizeof(rd)) < 0)
	{
		return -EIO;
	}

	const uint16_t t_raw = ((uint16_t)rd[3] << 8) | rd[4];
	const uint16_t h_raw = ((uint16_t)rd[6] << 8) | rd[7];
	// SCD4x: T = -45 + 175*raw/65535 [C], RH = 100*raw/65535 [%].
	out->co2_ppm = 0; // not measured in rht-only mode
	out->temp_x100 = temp_ema.update(-4500 + (int32_t)(17500LL * t_raw / 65535));
	out->hum_x100 = (uint16_t)(10000LL * h_raw / 65535);

	const uint8_t pd[2] = {0x36, 0xE0}; // power_down
	i2c_write_dt(&scd41_bus, pd, sizeof(pd));
	return 0;
}
