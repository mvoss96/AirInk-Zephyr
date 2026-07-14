/*
 * Copyright (c) 2025 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#pragma once

#include <platform/CHIPDeviceLayer.h>

/** Brings up Matter, then hands the device itself to AirInk.
 *
 * StartApp() runs the CHIP task queue on the main thread forever. Everything AirInk does --
 * the panel, the SCD41, the button, the menu -- runs on a thread of its own (app::run(), see
 * src/app.hpp), because a full CO2 read blocks for ~5 s and an e-paper refresh for ~2 s, and
 * neither may sit on the CHIP event loop while OpenThread waits to poll its parent.
 *
 * Direction of travel: AirInk measures, and hands each reading to Matter. Matter never reaches
 * back into the UI -- the network state is left where the loop picks it up (net::set_thread_connected()).
 */
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
};
