/*
 * Copyright (c) 2025 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#pragma once

#include "board/board.h"

#include <platform/CHIPDeviceLayer.h>

struct Identify;

#ifndef CONFIG_NCS_SAMPLE_MATTER_USE_DEFAULT_BUTTON_HANDLER
enum class ButtonState { None, SoftwareUpdate, UAT };
#endif

class AppTask {
public:
	static AppTask &Instance()
	{
		static AppTask sAppTask;
		return sAppTask;
	};

	CHIP_ERROR StartApp();

private:
	CHIP_ERROR Init();
	k_timer mTimer;

	/* One tick reads the SCD41 (temperature, humidity, CO2) and the battery, then publishes
	 * all of it. Matches AirInk's own 30 s cycle. The sensor is in periodic mode, so a read
	 * returns immediately -- unlike AirInk's single-shot path, which blocks ~5 s and would
	 * stall the CHIP thread this timer's work runs on. */
	static constexpr uint32_t kMeasurementIntervalMs = 30000;

	static void MeasurementTimeoutCallback(k_timer *timer);

	/* Runs on the CHIP thread (via ScheduleWork): reads the sensors and writes the clusters. */
	static void PublishMeasurements();

	static void ButtonEventHandler(Nrf::ButtonState state, Nrf::ButtonMask hasChanged);
};
