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

	/* The last full read's CO2, carried into the cheap T+RH readings so every Scd41Reading is
	 * complete -- the same kind of memory as temp_ema. Cleared by a recalibration, whose whole
	 * point is that the old number no longer describes the air. */
	uint16_t last_co2_ppm;

	/** The SCD4x's CRC-8 over one 16-bit word: polynomial 0x31, init 0xFF, no final XOR.
	 *
	 * @param w the two data bytes, most significant first
	 * @return the checksum the sensor sends after them
	 */
	uint8_t crc8(const uint8_t *w)
	{
		uint8_t crc = 0xFF;
		for (int i = 0; i < 2; i++)
		{
			crc ^= w[i];
			for (int b = 0; b < 8; b++)
			{
				crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x31) : (uint8_t)(crc << 1);
			}
		}
		return crc;
	}

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

	/** Write one SCD41 setting (into RAM; gone on power loss). val1 + val2/1e6, because the driver
	 * reads the offset as a real number of degrees -- whole degrees would silently drop the half
	 * the user just dialled in.
	 * @retval -EIO the sensor would not take it (logged as `what`) */
	int put(int attr, int32_t val1, int32_t val2, const char *what)
	{
		struct sensor_value sv{};
		sv.val1 = val1;
		sv.val2 = val2;
		if (sensor_attr_set(scd41_dev, SENSOR_CHAN_CO2, (enum sensor_attribute)attr, &sv) < 0)
		{
			printk("SCD41: could not set %s\n", what);
			return -EIO;
		}
		return 0;
	}

	/** Leave periodic measurement and wait for the sensor to go idle.
	 *
	 * @retval 0     idle, ready for a command
	 * @retval -EIO  the I2C write failed
	 */
	int stop_periodic()
	{
		const uint8_t stop[2] = {0x3F, 0x86}; // stop_periodic_measurement
		if (i2c_write_dt(&scd41_bus, stop, sizeof(stop)) < 0)
		{
			return -EIO;
		}
		k_msleep(500); // datasheet: no command is accepted for 500 ms afterwards
		return 0;
	}

	/** Back to the single-shot regime's resting state (command 0x36E0).
	 *
	 * @retval 0     the sensor is asleep
	 * @retval -EIO  the I2C write failed
	 */
	int power_down()
	{
		const uint8_t pd[2] = {0x36, 0xE0};
		return (i2c_write_dt(&scd41_bus, pd, sizeof(pd)) < 0) ? -EIO : 0;
	}

} // namespace

int scd41::init()
{
	if (!device_is_ready(scd41_dev))
	{
		return -ENODEV;
	}
	// No trim here, deliberately -- the header says why.
	return 0;
}

int scd41::set_trim(const Trim &t)
{
	if (!device_is_ready(scd41_dev))
	{
		return -ENODEV;
	}

	// The driver's attr_set does not wake the sensor, and in single-shot mode it is asleep between
	// readings -- so a setting written to a sleeping SCD41 is NACKed and quietly lost.
	wake();

	int err = 0;
	err |= put(SENSOR_ATTR_SCD4X_TEMPERATURE_OFFSET, t.temp_offset_x10 / 10,
			   (t.temp_offset_x10 % 10) * 100000, "temperature offset");
	err |= put(SENSOR_ATTR_SCD4X_SENSOR_ALTITUDE, t.altitude_m, 0, "altitude");
	err |= put(SENSOR_ATTR_SCD4X_AUTOMATIC_CALIB_ENABLE, t.auto_calib ? 1 : 0, 0,
			   "self-calibration");

	// Back to sleep -- this path is called from the menu, when the loop takes no readings, so
	// nothing else would come along to do it. A sensor left idling until the menu times out is the
	// kind of leak that never shows in a log and does show in the battery.
	power_down();

	// Deliberately NOT persisted: scd4x_persist_settings() writes the sensor's EEPROM (a few
	// thousand writes of life). prefs re-pushes on every boot instead -- three I2C transfers.
	printk("[SCD41] trim: offset %d.%d C, altitude %d m, self-calib %s%s\n", t.temp_offset_x10 / 10,
		   abs(t.temp_offset_x10 % 10), t.altitude_m, t.auto_calib ? "on" : "off",
		   err ? "  (INCOMPLETE)" : "");
	return err;
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

	last_co2_ppm = (uint16_t)co2.val1;
	out->co2_ppm = last_co2_ppm;
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
		power_down(); // every way out of here puts the sensor back to sleep -- see below
		return -EIO;
	}
	k_msleep(60); // rht-only measurement time (< 50 ms per datasheet)

	const uint8_t rd_cmd[2] = {0xEC, 0x05}; // read_measurement
	uint8_t rd[9];							// CO2+crc, T+crc, RH+crc
	if (i2c_write_dt(&scd41_bus, rd_cmd, sizeof(rd_cmd)) < 0 ||
		i2c_read_dt(&scd41_bus, rd, sizeof(rd)) < 0)
	{
		power_down();
		return -EIO;
	}

	/* Checked, because a corrupted word does not look like an error -- it looks like a temperature,
	 * goes into the EMA, and lingers for several ticks. This path runs 9 of 10 cycles. */
	if (crc8(&rd[3]) != rd[5] || crc8(&rd[6]) != rd[8])
	{
		printk("SCD41: bad CRC on the T/RH read\n");
		power_down();
		return -EIO;
	}

	const uint16_t t_raw = ((uint16_t)rd[3] << 8) | rd[4];
	const uint16_t h_raw = ((uint16_t)rd[6] << 8) | rd[7];
	// SCD4x: T = -45 + 175*raw/65535 [C], RH = 100*raw/65535 [%].
	out->co2_ppm = last_co2_ppm; // not measured in rht-only mode; the last full read stands in
	out->temp_x100 = temp_ema.update(-4500 + (int32_t)(17500LL * t_raw / 65535));
	out->hum_x100 = (uint16_t)(10000LL * h_raw / 65535);

	power_down();
	return 0;
}

int scd41::calibrate_begin()
{
	wake();

	const uint8_t start[2] = {0x21, 0xB1}; // start_periodic_measurement
	if (i2c_write_dt(&scd41_bus, start, sizeof(start)) < 0)
	{
		power_down(); // woken and then refused: do not leave it idling for the next half minute
		return -EIO;
	}
	return 0;
}

int scd41::calibrate_finish(uint16_t target_ppm, int16_t *correction_ppm)
{
	// Every failure below powers the sensor down on its way out. Without that a failed
	// stop leaves it drawing ~50 mA every 5 s, and a failed FRC leaves it idle rather
	// than asleep -- in both cases until some later measurement happens to clean up.
	if (stop_periodic() < 0)
	{
		power_down();
		return -EIO;
	}

	/* stop_periodic() by hand, because the driver's set_idle_mode() branches on the DEVICETREE mode
	 * (single-shot) and would send wake_up instead of stopping the periodic run we started. From
	 * here the driver does the right thing: FRC, then power_down -- our normal resting state. */
	uint16_t correction = 0;
	if (scd4x_forced_recalibration(scd41_dev, target_ppm, &correction) < 0)
	{
		power_down(); // the driver only reaches its own on the success path
		return -EIO;  // includes the sensor's own 0xFFFF "I do not believe that reading"
	}

	// The held CO2 value predates the correction and must not stand in for another reading. The
	// caller is expected to take a full sample next (app restarts its cadence); until then a
	// T+RH reading carries 0 rather than a number the recalibration just disowned.
	last_co2_ppm = 0;

	*correction_ppm = (int16_t)correction; // the driver already removed the 0x8000 bias
	return 0;
}

int scd41::calibrate_abort()
{
	const int err = stop_periodic();
	return (power_down() < 0 || err < 0) ? -EIO : 0;
}
