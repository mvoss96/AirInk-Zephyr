/*
 * Copyright (c) 2025 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include "app_task.h"

#include "app/matter_init.h"
#include "app/task_executor.h"
#include "board/board.h"
#include "clusters/identify.h"
#include "lib/core/CHIPError.h"

#include "sensors/battery.hpp"

#include <app-common/zap-generated/attributes/Accessors.h>
#include <app/clusters/air-quality-server/air-quality-server.h>
#include <app/clusters/concentration-measurement-server/concentration-measurement-server.h>
#include <app/clusters/power-source-server/power-source-server.h>

#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(app, CONFIG_CHIP_APP_LOG_LEVEL);

using namespace ::chip;
using namespace ::chip::app;
using namespace ::chip::DeviceLayer;
using namespace ::chip::app::Clusters;

namespace
{
/* Endpoint map -- see src/default_zap/airink.zap. One physical sensor, one endpoint: the Air
 * Quality Sensor device type may carry temperature and humidity alongside the CO2 clusters, so
 * everything the SCD41 measures lives on endpoint 1. (Splitting it into a temperature, humidity
 * and air quality endpoint also works, but each device type mandates its own Identify cluster,
 * and a controller then shows three "Identify" entities for one device.) */
constexpr EndpointId kPowerSourceEndpointId = 0; /* root node */
constexpr EndpointId kSensorEndpointId = 1;

/* The battery powers the whole node. */
EndpointId sPoweredEndpoints[] = { 0, 1 };

const device *sSensorDev = DEVICE_DT_GET(DT_NODELABEL(scd41));

Nrf::Matter::IdentifyCluster sIdentifyCluster(kSensorEndpointId);

/* Unlike temperature and humidity, the air quality and concentration clusters are not backed
 * by the ember attribute store -- they are served by these AttributeAccessInterface instances,
 * which is why the ZAP marks their attributes "External". The concentration template flags are
 * <Numeric, Level, MediumLevel, CriticalLevel, Peak, Average>: we report a numeric ppm value
 * and nothing else, and the ZAP's attribute list must match that exactly. */
AirQuality::Instance sAirQuality(kSensorEndpointId,
				 BitMask<AirQuality::Feature, uint32_t>(AirQuality::Feature::kModerate,
									AirQuality::Feature::kFair,
									AirQuality::Feature::kVeryPoor,
									AirQuality::Feature::kExtremelyPoor));

ConcentrationMeasurement::Instance<true, false, false, false, false, false>
	sCo2(kSensorEndpointId, CarbonDioxideConcentrationMeasurement::Id,
	     ConcentrationMeasurement::MeasurementMediumEnum::kAir,
	     ConcentrationMeasurement::MeasurementUnitEnum::kPpm);

/* SCD41 datasheet operating range. */
constexpr float kCo2MinPpm = 400.0f;
constexpr float kCo2MaxPpm = 5000.0f;

/* CO2 as a proxy for air quality. The thresholds are the usual indoor-air guidance (Pettenkofer's
 * 1000 ppm is the "still fine" line; 2000 ppm is where concentration measurably suffers). */
AirQuality::AirQualityEnum AirQualityFromCo2(uint16_t ppm)
{
	if (ppm < 800) {
		return AirQuality::AirQualityEnum::kGood;
	}
	if (ppm < 1000) {
		return AirQuality::AirQualityEnum::kFair;
	}
	if (ppm < 1400) {
		return AirQuality::AirQualityEnum::kModerate;
	}
	if (ppm < 2000) {
		return AirQuality::AirQualityEnum::kPoor;
	}
	if (ppm < 5000) {
		return AirQuality::AirQualityEnum::kVeryPoor;
	}
	return AirQuality::AirQualityEnum::kExtremelyPoor;
}

#ifdef CONFIG_CHIP_ICD_UAT_SUPPORT
#ifdef CONFIG_NCS_SAMPLE_MATTER_USE_DEFAULT_BUTTON_HANDLER
#define UAT_BUTTON_MASK DK_BTN3_MSK
#endif
#endif

#ifndef CONFIG_NCS_SAMPLE_MATTER_USE_DEFAULT_BUTTON_HANDLER
constexpr int kSoftwareUpdateTimeout = 1500;
constexpr int kUatTimeout = 1500;
constexpr int kFactoryResetTimeout = 3000;
constexpr int kUatBlinkPeriod = 200;
ButtonState sBtnState = ButtonState::None;
k_timer sBtn1Timer;
#endif

} /* namespace */

void HandleUAT()
{
#ifdef CONFIG_CHIP_ICD_UAT_SUPPORT
	LOG_INF("ICD UserActiveMode has been triggered.");
	Server::GetInstance().GetICDManager().OnNetworkActivity();
#endif
}
/* nRF54T15 TAG has only a single button,
 * therefore the default handler for factory data and software update had to be overriden.
 * On other DK's only UAT is handled by the application.
 */
void AppTask::ButtonEventHandler(Nrf::ButtonState state, Nrf::ButtonMask hasChanged)
{
#ifdef CONFIG_NCS_SAMPLE_MATTER_USE_DEFAULT_BUTTON_HANDLER
	if (UAT_BUTTON_MASK & state & hasChanged) {
		HandleUAT();
	}
#else
	if (DK_BTN1_MSK & hasChanged) {
		if (DK_BTN1_MSK & state) {
			LOG_INF("Release the button within %ums to trigger Software Update", kSoftwareUpdateTimeout);
			k_timer_start(&sBtn1Timer, K_MSEC(kSoftwareUpdateTimeout), K_NO_WAIT);
			sBtnState = ButtonState::SoftwareUpdate;
		} else {
			if (sBtnState == ButtonState::SoftwareUpdate) {
#ifndef CONFIG_NCS_SAMPLE_MATTER_CUSTOM_BLUETOOTH_ADVERTISING
				if (Nrf::GetBoard().GetDeviceState() == Nrf::DeviceState::DeviceProvisioned) {
/* In this case we need to run only Bluetooth LE SMP advertising if it is available */
#ifdef CONFIG_CHIP_DFU_OVER_BT_SMP
					GetDFUOverSMP().StartServer();
#else
					LOG_INF("Software update is disabled");
#endif
				} else {
					/* In this case we start both Bluetooth LE SMP and Matter advertising at the
					 * same time */
					Nrf::GetBoard().StartBLEAdvertisement();
				}
#endif
			} else if (sBtnState == ButtonState::UAT) {
				HandleUAT();
			}
			/* Restore LED's state and cancel the timer */
			k_timer_stop(&sBtn1Timer);
			sBtnState = ButtonState::None;
			Nrf::GetBoard().RestoreAllLedsState();
			Nrf::GetBoard().RunLedStateHandler();
		}
	}
#endif
}
#ifndef CONFIG_NCS_SAMPLE_MATTER_USE_DEFAULT_BUTTON_HANDLER
void ButtonTimerEventHandler()
{
	if (sBtnState == ButtonState::SoftwareUpdate) {
		LOG_INF("Release the button within %ums to trigger UAT", kUatTimeout);
		k_timer_start(&sBtn1Timer, K_MSEC(kUatTimeout), K_NO_WAIT);
		sBtnState = ButtonState::UAT;

		/* Turn off all LEDs before starting blink to make sure blink is coordinated. */
		Nrf::GetBoard().ResetAllLeds();
		Nrf::GetBoard().ForEachLED([](Nrf::LEDWidget &led) { led.Blink(kUatBlinkPeriod); });
	} else if (sBtnState == ButtonState::UAT) {
		LOG_INF("Factory reset has been triggered. Release button within %ums to cancel.",
			kFactoryResetTimeout);

		/* Start timer for sFactoryResetTimeout to allow user to cancel, if required. */
		k_timer_start(&sBtn1Timer, K_MSEC(kFactoryResetTimeout), K_NO_WAIT);
		sBtnState = ButtonState::None;

		Nrf::GetBoard().ForEachLED([](Nrf::LEDWidget &led) { led.Blink(Nrf::LedConsts::kBlinkRate_ms); });
		/* If we reached here, the button was held past FactoryResetTriggerTimeout, initiate factory reset */
	} else if (sBtnState == ButtonState::None) {
		/* Actually trigger Factory Reset */
		chip::Server::GetInstance().ScheduleFactoryReset();
	}
}

void ButtonTimerTimeoutCallback(k_timer *timer)
{
	Nrf::PostTask([] { ButtonTimerEventHandler(); });
}

#endif

void AppTask::PublishMeasurements()
{
	int err = sensor_sample_fetch(sSensorDev);
	if (err) {
		LOG_ERR("SCD41 sample fetch failed: %d", err);
	} else {
		sensor_value temperature;
		sensor_value humidity;
		sensor_value co2;

		/* Temperature: the cluster wants centi-degC. val2 is millionths of a degree, so
		 * /10000 lands on the same scale. Humidity is centi-percent, same arithmetic. */
		if (sensor_channel_get(sSensorDev, SENSOR_CHAN_AMBIENT_TEMP, &temperature) == 0) {
			const int16_t centi = static_cast<int16_t>(temperature.val1 * 100 + temperature.val2 / 10000);
			TemperatureMeasurement::Attributes::MeasuredValue::Set(kSensorEndpointId, centi);
		}

		if (sensor_channel_get(sSensorDev, SENSOR_CHAN_HUMIDITY, &humidity) == 0) {
			const uint16_t centi = static_cast<uint16_t>(humidity.val1 * 100 + humidity.val2 / 10000);
			RelativeHumidityMeasurement::Attributes::MeasuredValue::Set(kSensorEndpointId, centi);
		}

		/* CO2: the concentration cluster carries a float in the unit we declared (ppm). */
		if (sensor_channel_get(sSensorDev, SENSOR_CHAN_CO2, &co2) == 0) {
			const uint16_t ppm = static_cast<uint16_t>(co2.val1);
			sCo2.SetMeasuredValue(DataModel::Nullable<float>(static_cast<float>(ppm)));
			sAirQuality.UpdateAirQuality(AirQualityFromCo2(ppm));
			LOG_DBG("SCD41: %d.%02d C, %d.%02d %%, %u ppm", temperature.val1,
				temperature.val2 / 10000, humidity.val1, humidity.val2 / 10000, ppm);
		}
	}

	const battery::State bat = battery::read();

	/* Matter counts the battery in half percent. */
	PowerSource::Attributes::BatPercentRemaining::Set(kPowerSourceEndpointId,
							  static_cast<uint8_t>(bat.pct * 2));
	PowerSource::Attributes::BatChargeState::Set(kPowerSourceEndpointId,
						     bat.charging ? PowerSource::BatChargeStateEnum::kIsCharging
								  : PowerSource::BatChargeStateEnum::kIsNotCharging);
	PowerSource::Attributes::BatChargeLevel::Set(kPowerSourceEndpointId,
						     bat.low ? PowerSource::BatChargeLevelEnum::kCritical
							     : PowerSource::BatChargeLevelEnum::kOk);
}

void AppTask::MeasurementTimeoutCallback(k_timer *timer)
{
	/* Cluster attributes may only be touched from the CHIP thread. */
	DeviceLayer::PlatformMgr().ScheduleWork([](intptr_t) { AppTask::PublishMeasurements(); }, 0);
}

CHIP_ERROR AppTask::Init()
{
	/* Initialize Matter stack */
	ReturnErrorOnFailure(Nrf::Matter::PrepareServer());

	if (!Nrf::GetBoard().Init(ButtonEventHandler)) {
		LOG_ERR("User interface initialization failed.");
		return CHIP_ERROR_INCORRECT_STATE;
	}

	/* Register Matter event handler that controls the connectivity status LED based on the captured Matter network
	 * state. */
	ReturnErrorOnFailure(Nrf::Matter::RegisterEventHandler(Nrf::Board::DefaultMatterEventHandler, 0));

	if (!device_is_ready(sSensorDev)) {
		LOG_ERR("SCD41 not ready");
		return chip::System::MapErrorZephyr(-ENODEV);
	}

	int err = battery::init();
	if (err) {
		LOG_ERR("Battery monitor init failed: %d", err);
		return chip::System::MapErrorZephyr(err);
	}

	ReturnErrorOnFailure(sIdentifyCluster.Init());
	ReturnErrorOnFailure(Nrf::Matter::StartServer());

	/* Only now. These three reach into the ember endpoint tables, which do not exist until
	 * Server::Init() has run emberAfEndpointConfigure() -- i.e. until StartServer() returns.
	 * AirQuality::Instance::Init() asserts on it with VerifyOrDie (a boot-time abort), the
	 * concentration instance merely returns an error and would then silently never publish.
	 * The event loop is live by now, so take the stack lock. */
	PlatformMgr().LockChipStack();
	err = 0;
	if (sAirQuality.Init() != CHIP_NO_ERROR || sCo2.Init() != CHIP_NO_ERROR) {
		err = -EIO;
	} else {
		/* Static facts about the source: the SCD41's range, and the endpoints it powers. */
		sCo2.SetMinMeasuredValue(DataModel::Nullable<float>(kCo2MinPpm));
		sCo2.SetMaxMeasuredValue(DataModel::Nullable<float>(kCo2MaxPpm));
		PowerSourceServer::Instance().SetEndpointList(kPowerSourceEndpointId,
							      Span<EndpointId>(sPoweredEndpoints));
	}
	PlatformMgr().UnlockChipStack();

	if (err) {
		LOG_ERR("Air quality / CO2 cluster init failed");
		return CHIP_ERROR_INCORRECT_STATE;
	}

	return CHIP_NO_ERROR;
}

CHIP_ERROR AppTask::StartApp()
{
	ReturnErrorOnFailure(Init());

	k_timer_init(&mTimer, AppTask::MeasurementTimeoutCallback, nullptr);
	k_timer_user_data_set(&mTimer, this);
	k_timer_start(&mTimer, K_MSEC(kMeasurementIntervalMs), K_MSEC(kMeasurementIntervalMs));
#ifndef CONFIG_NCS_SAMPLE_MATTER_USE_DEFAULT_BUTTON_HANDLER
	k_timer_init(&sBtn1Timer, &ButtonTimerTimeoutCallback, nullptr);
#endif
	while (true) {
		Nrf::DispatchNextTask();
	}

	return CHIP_NO_ERROR;
}
