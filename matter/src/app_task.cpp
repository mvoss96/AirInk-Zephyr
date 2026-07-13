/*
 * Copyright (c) 2025 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include "app_task.h"

#include "app/matter_init.h"
#include "app/matter_event_handler.h"
#include "app/task_executor.h"
#include "board/board.h"
#include "clusters/identify.h"
#include "lib/core/CHIPError.h"

#include "app.hpp"

#include <app-common/zap-generated/attributes/Accessors.h>
#include <app/clusters/air-quality-server/air-quality-server.h>
#include <app/clusters/concentration-measurement-server/concentration-measurement-server.h>
#include <app/clusters/power-source-server/power-source-server.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(app, CONFIG_CHIP_APP_LOG_LEVEL);

using namespace ::chip;
using namespace ::chip::app; /* NB: this makes a bare `app::` ambiguous with AirInk's own
					  namespace -- reach for the latter as ::app:: */
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

/* AirInk's own thread. It renders a 400x300 mono frame with 48 px fonts and blocks ~5 s in a
 * CO2 single-shot, so it gets the big stack -- and it gets it here rather than on the CHIP event
 * loop, which OpenThread needs responsive. Priority below the cooperative CHIP/OT threads. */
K_THREAD_STACK_DEFINE(sAirInkStack, 8192);
k_thread sAirInkThread;

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

/* Both hooks run on AirInk's thread, so they take the CHIP stack lock -- the documented way to
 * touch cluster attributes from outside the event loop. Holding it across four setters keeps the
 * reading self-consistent for anyone reading it at that moment; none of them block. */
void PublishReading(const Scd41Reading &r)
{
	PlatformMgr().LockChipStack();
	TemperatureMeasurement::Attributes::MeasuredValue::Set(kSensorEndpointId,
							       static_cast<int16_t>(r.temp_x100));
	RelativeHumidityMeasurement::Attributes::MeasuredValue::Set(kSensorEndpointId, r.hum_x100);
	sCo2.SetMeasuredValue(DataModel::Nullable<float>(static_cast<float>(r.co2_ppm)));
	sAirQuality.UpdateAirQuality(AirQualityFromCo2(r.co2_ppm));
	PlatformMgr().UnlockChipStack();
}

void PublishBattery(const battery::State &bat)
{
	PlatformMgr().LockChipStack();
	/* Matter counts the battery in half percent. */
	PowerSource::Attributes::BatPercentRemaining::Set(kPowerSourceEndpointId,
							  static_cast<uint8_t>(bat.pct * 2));
	PowerSource::Attributes::BatChargeState::Set(kPowerSourceEndpointId,
						     bat.charging ? PowerSource::BatChargeStateEnum::kIsCharging
								  : PowerSource::BatChargeStateEnum::kIsNotCharging);
	PowerSource::Attributes::BatChargeLevel::Set(kPowerSourceEndpointId,
						     bat.low ? PowerSource::BatChargeLevelEnum::kCritical
							     : PowerSource::BatChargeLevelEnum::kOk);
	PlatformMgr().UnlockChipStack();
}

/* Runs on the CHIP thread. Only records the radio state; the status bar belongs to AirInk's
 * thread, which picks the value up on its next cycle. */
void MatterEventHandler(const ChipDeviceEvent *event, intptr_t)
{
	switch (event->Type) {
	case DeviceEventType::kCHIPoBLEAdvertisingChange:
		::app::set_link(event->CHIPoBLEAdvertisingChange.Result == kActivity_Started
				      ? ui::Link::BleAdv
				      : ui::Link::None);
		break;
	case DeviceEventType::kThreadConnectivityChange:
		::app::set_link(event->ThreadConnectivityChange.Result == kConnectivity_Established
				      ? ui::Link::ThreadConnected
				      : ui::Link::ThreadJoining);
		break;
	default:
		break;
	}
}

void AirInkThread(void *, void *, void *)
{
	const ::app::Hooks hooks = { .reading = PublishReading, .battery = PublishBattery };
	::app::set_hooks(hooks);
	::app::run(); /* never returns */
}

} /* namespace */

CHIP_ERROR AppTask::Init()
{
	ReturnErrorOnFailure(Nrf::Matter::PrepareServer());
	ReturnErrorOnFailure(Nrf::Matter::RegisterEventHandler(MatterEventHandler, 0));
	ReturnErrorOnFailure(sIdentifyCluster.Init());
	ReturnErrorOnFailure(Nrf::Matter::StartServer());

	/* Only now. These three reach into the ember endpoint tables, which do not exist until
	 * Server::Init() has run emberAfEndpointConfigure() -- i.e. until StartServer() returns.
	 * AirQuality::Instance::Init() asserts on it with VerifyOrDie (a boot-time abort), the
	 * concentration instance merely returns an error and would then silently never publish.
	 * The event loop is live by now, so take the stack lock. */
	CHIP_ERROR err = CHIP_NO_ERROR;
	PlatformMgr().LockChipStack();
	err = sAirQuality.Init();
	if (err == CHIP_NO_ERROR) {
		err = sCo2.Init();
	}
	if (err == CHIP_NO_ERROR) {
		/* Static facts about the source: the SCD41's range, and the endpoints it powers. */
		sCo2.SetMinMeasuredValue(DataModel::Nullable<float>(kCo2MinPpm));
		sCo2.SetMaxMeasuredValue(DataModel::Nullable<float>(kCo2MaxPpm));
		PowerSourceServer::Instance().SetEndpointList(kPowerSourceEndpointId,
							      Span<EndpointId>(sPoweredEndpoints));
	}
	PlatformMgr().UnlockChipStack();

	return err;
}

CHIP_ERROR AppTask::StartApp()
{
	ReturnErrorOnFailure(Init());

	k_thread_create(&sAirInkThread, sAirInkStack, K_THREAD_STACK_SIZEOF(sAirInkStack), AirInkThread,
			nullptr, nullptr, nullptr, K_PRIO_PREEMPT(10), 0, K_NO_WAIT);
	k_thread_name_set(&sAirInkThread, "airink");

	while (true) {
		Nrf::DispatchNextTask();
	}

	return CHIP_NO_ERROR;
}
