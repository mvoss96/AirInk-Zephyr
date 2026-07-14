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
#include <app/clusters/unit-localization-server/unit-localization-server.h>
#include <app/server/Server.h>
#include <openthread/thread.h>
#include <platform/ThreadStackManager.h>
#include <setup_payload/OnboardingCodesUtil.h>
#include <setup_payload/QRCodeSetupPayloadGenerator.h>

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

	/* Riding along, because this is the one place that already holds the lock on AirInk's own
	 * thread: whether we are on a fabric, which is what decides if the menu's Matter row is a way
	 * in to the QR or a statement of fact.
	 *
	 * Polled rather than driven by an event, because there is no event for a fabric being
	 * *removed* -- CHIP only signals kCommissioningComplete. The table is the truth, and this runs
	 * on every wake of the loop, so the flag is fresh by the time the menu opens (the loop calls
	 * this hook before it acts on the button). */
	::app::set_commissioned(Server::GetInstance().GetFabricTable().FabricCount() > 0);
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

/* The onboarding codes, fetched once from CHIP. Static because app::set_pairing_codes() does not
 * copy them and the UI holds the pointers for the life of the device. */
char sQrCode[chip::QRCodeBasicSetupPayloadGenerator::kMaxQRCodeBase38RepresentationLength + 1];
char sManualCode[chip::kManualSetupLongCodeCharLength + 1];
char sManualCodePretty[sizeof(sManualCode) + 2]; /* room for the two dashes */

/* CHIP hands the manual code back as bare digits ("35358600323"), which is what a controller
 * wants to parse but not what a human wants to type: eleven undifferentiated digits are read
 * wrong. Every controller UI groups them 4-3-4, so group them here too.
 *
 * Only the 11-digit short code is grouped -- the 21-digit long code (VID/PID embedded) has its
 * own layout, and we do not generate it. Anything unexpected is passed through unchanged rather
 * than mangled.
 */
const char *GroupManualCode(const char *digits)
{
	if (strlen(digits) != 11) {
		return digits;
	}
	snprintf(sManualCodePretty, sizeof(sManualCodePretty), "%.4s-%.3s-%.4s", digits, digits + 4,
		 digits + 7);
	return sManualCodePretty;
}

/* What a controller needs to find and authenticate this device -- the same payload the sample
 * prints to the log at boot, but printed on the e-paper, where the user actually is. It does not
 * change over the device's life, so read it once and hand it to the UI. */
void FetchOnboardingCodes()
{
	chip::MutableCharSpan qr(sQrCode);
	chip::MutableCharSpan manual(sManualCode);
	const chip::RendezvousInformationFlags rendezvous(chip::RendezvousInformationFlag::kBLE);

	if (GetQRCode(qr, rendezvous) != CHIP_NO_ERROR ||
	    GetManualPairingCode(manual, rendezvous) != CHIP_NO_ERROR) {
		LOG_ERR("Could not read the onboarding codes; no pairing screen");
		return;
	}

	::app::set_pairing_codes(sQrCode, GroupManualCode(sManualCode));
}

/* How long the radio listens after the user asks for the code. Ten minutes is what it takes to
 * fetch a phone and scan; it is not how long an accidental visit to the menu should leave the door
 * open. This is deliberately NOT the boot window: OpenBasicCommissioningWindow() defaults to
 * CHIP_DEVICE_CONFIG_DISCOVERY_TIMEOUT_SECS, which the nRF platform derives from
 * CONFIG_CHIP_BLE_ADVERTISING_DURATION (60 min) -- right for a factory-new device that nobody has
 * touched yet, too generous for a gesture. */
constexpr auto kPanelPairingWindow = System::Clock::Seconds32(10 * 60);

/* The user just put the onboarding code on the panel, so start listening for whoever scans it.
 *
 * The code alone is not an invitation. CHIP advertises for an hour after boot and then stops, so on
 * a device that has been sitting on a shelf the QR would be a dead letter -- shown, scanned, and
 * never answered. Opening the window here is what makes the screen mean what it says.
 *
 * Runs on AirInk's thread, so it takes the stack lock, like the publish hooks above.
 *
 * The two guards mirror Nordic's own Board::StartBLEAdvertisement (nrf/samples/matter/common):
 * a commissioned device must not re-open (a second commissioner could join uninvited -- and the
 * menu does not offer this path there anyway), and an already-advertising one must not be
 * restarted, which would drop a PASE session that may be seconds from completing. Note the second
 * guard also means a device still inside its boot hour keeps that hour: this call does not shorten
 * a window that is already open.
 */
void OpenPairingWindow()
{
	PlatformMgr().LockChipStack();

	if (Server::GetInstance().GetFabricTable().FabricCount() != 0) {
		LOG_INF("Already commissioned; not re-opening the window");
	} else if (ConnectivityMgr().IsBLEAdvertisingEnabled()) {
		LOG_INF("Commissioning window already open");
	} else if (Server::GetInstance().GetCommissioningWindowManager().OpenBasicCommissioningWindow(
			   kPanelPairingWindow) != CHIP_NO_ERROR) {
		LOG_ERR("Could not open the commissioning window");
	} else {
		LOG_INF("Commissioning window opened from the panel for %u s",
			kPanelPairingWindow.count());
	}

	PlatformMgr().UnlockChipStack();
}

/* The user pressed the boot onboarding screen away. They saw the code and did not want it (yet), so
 * the hour that CONFIG_CHIP_ENABLE_PAIRING_AUTOSTART opened is now an hour of advertising nobody
 * asked for. Cut it to the same ten minutes the menu grants -- enough time to change their mind and
 * fetch a phone, not enough to leave the door open all afternoon.
 *
 * There is no "shorten" in CHIP, so this closes and re-opens. That is safe precisely here and
 * nowhere else: the user just pressed a button on a device that is not commissioned, which is not
 * something anyone does in the middle of scanning it. (The menu's OpenPairingWindow deliberately
 * does NOT do this -- it leaves an open window alone, because there it really could be interrupting
 * a PASE session.)
 */
void ShortenPairingWindow()
{
	PlatformMgr().LockChipStack();

	auto &window = Server::GetInstance().GetCommissioningWindowManager();
	if (Server::GetInstance().GetFabricTable().FabricCount() != 0) {
		LOG_INF("Commissioned in the meantime; leaving the window alone");
	} else {
		window.CloseCommissioningWindow();
		if (window.OpenBasicCommissioningWindow(kPanelPairingWindow) != CHIP_NO_ERROR) {
			LOG_ERR("Could not re-open the commissioning window");
		} else {
			LOG_INF("Onboarding dismissed; window now closes in %u s",
				kPanelPairingWindow.count());
		}
	}

	PlatformMgr().UnlockChipStack();
}

/* The user held the button on a confirmation screen that said what this does. CHIP wipes the
 * fabrics and reboots; it schedules the work rather than doing it here, because we are on AirInk's
 * thread and the storage belongs to the CHIP one. */
void FactoryReset()
{
	LOG_INF("Factory reset requested from the panel");
	Server::GetInstance().ScheduleFactoryReset();
}

/* The Unit Localization cluster (0x002D on the root node) is how a controller reads and sets the unit
 * the device displays. Note what it is NOT: the temperature itself stays Celsius on the wire, always
 * -- Matter reports centi-degrees Celsius and Home Assistant renders them in whatever the user's
 * profile says. This cluster is about the panel, and only the panel.
 *
 * The SDK's server is a closed singleton: it holds the value, persists it, and reports it, and hands
 * the application neither a delegate nor a callback. So the traffic goes both ways by hand -- pushed
 * out here, polled back in by the loop (see ::app::Hooks::unit_from_network). */
ui::TempUnit FromMatter(UnitLocalization::TempUnitEnum u)
{
	return u == UnitLocalization::TempUnitEnum::kFahrenheit ? ui::TempUnit::Fahrenheit
							       : ui::TempUnit::Celsius;
}

void PublishUnit(ui::TempUnit u)
{
	const auto unit = (u == ui::TempUnit::Fahrenheit) ? UnitLocalization::TempUnitEnum::kFahrenheit
							  : UnitLocalization::TempUnitEnum::kCelsius;

	PlatformMgr().LockChipStack();
	const CHIP_ERROR err = UnitLocalization::UnitLocalizationServer::Instance().SetTemperatureUnit(unit);
	PlatformMgr().UnlockChipStack();

	if (err != CHIP_NO_ERROR) {
		LOG_ERR("Could not publish the temperature unit (%s)", err.AsString());
	}
}

bool UnitFromNetwork(ui::TempUnit *out)
{
	PlatformMgr().LockChipStack();
	*out = FromMatter(UnitLocalization::UnitLocalizationServer::Instance().GetTemperatureUnit());
	PlatformMgr().UnlockChipStack();
	return true;
}

/* How well the parent router hears us, for the status bar's bars.
 *
 * The average and not the last packet's RSSI: a single frame is noise, and the panel is not a
 * spectrum analyser -- it is a hint about where to put the thing. OpenThread already keeps the
 * average, so this is a read, not a measurement, and costs no radio time.
 *
 * Fails, correctly, whenever there is no parent to be heard by: not joined yet, or -- if this device
 * ever stops being a sleepy child -- a router itself, which has no parent at all. */
bool LinkRssi(int8_t *out)
{
	ThreadStackMgr().LockThreadStack();
	const otError err = otThreadGetParentAverageRssi(ThreadStackMgrImpl().OTInstance(), out);
	ThreadStackMgr().UnlockThreadStack();

	return err == OT_ERROR_NONE && *out != OT_RADIO_RSSI_INVALID;
}

void AirInkThread(void *, void *, void *)
{
	const ::app::Hooks hooks = {
		.reading = PublishReading,
		.battery = PublishBattery,
		.factory_reset = FactoryReset,
		.pairing_open = OpenPairingWindow,
		.pairing_dismissed = ShortenPairingWindow,
		.publish_unit = PublishUnit,
		.unit_from_network = UnitFromNetwork,
		.link_rssi = LinkRssi,
	};
	::app::set_hooks(hooks);
	::app::set_build_name("Matter over Thread");
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

	/* Before the thread starts: app::run() reads the codes when it builds the UI, and whether
	 * there are any is what decides that the menu has a Matter row at all. The fabric table has
	 * been loaded from NVS by now, so this is the state a reboot comes back to. */
	FetchOnboardingCodes();
	::app::set_commissioned(Server::GetInstance().GetFabricTable().FabricCount() > 0);

	k_thread_create(&sAirInkThread, sAirInkStack, K_THREAD_STACK_SIZEOF(sAirInkStack), AirInkThread,
			nullptr, nullptr, nullptr, K_PRIO_PREEMPT(10), 0, K_NO_WAIT);
	k_thread_name_set(&sAirInkThread, "airink");

	while (true) {
		Nrf::DispatchNextTask();
	}

	return CHIP_NO_ERROR;
}
