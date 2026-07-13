#
# Copyright (c) 2025 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

cmake_minimum_required(VERSION 3.20.0)

# The custom solomon,ssd1683 binding lives with the standalone app, one level up. Zephyr only
# looks for dts/bindings under the application directory, which is this one -- point it there
# too, or the e-paper node's width/height properties silently do not exist.
list(APPEND DTS_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)

# The board's drivers, shared with the standalone firmware. Merged after prj.conf, so keep it free
# of anything an app would want to override -- see the file's header.
list(APPEND EXTRA_CONF_FILE ${CMAKE_CURRENT_SOURCE_DIR}/../conf/airink_hw.conf)

find_package(Zephyr HINTS $ENV{ZEPHYR_BASE})

project(matter-temperature-sensor)

# Enable GNU STD support.
include(${ZEPHYR_CONNECTEDHOMEIP_MODULE_DIR}/config/nrfconnect/app/enable-gnu-std.cmake)

# NORDIC SDK APP START

# Include all source files that are located in the Matter common directory.
include(${ZEPHYR_NRF_MODULE_DIR}/samples/matter/common/cmake/source_common.cmake)

# Include Data Model utils
include(${ZEPHYR_NRF_MODULE_DIR}/samples/matter/common/cmake/data_model.cmake)

include(${ZEPHYR_NRF_MODULE_DIR}/samples/matter/common/cmake/zap_helpers.cmake)
ncs_get_zap_parent_dir(ZAP_PARENT_DIR)

target_include_directories(app PRIVATE
  src
  ${ZAP_PARENT_DIR}
  # AirInk's own source tree, one level up.
  ${CMAKE_CURRENT_SOURCE_DIR}/../src
)

target_sources(app PRIVATE
  src/app_task.cpp
  src/main.cpp
)

# The AirInk device: the same sources the standalone firmware builds, minus its main() -- here
# app::run() is one thread among the CHIP event loop and OpenThread. Keep this list in step with
# the root CMakeLists.txt.
set(AIRINK_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_sources(app PRIVATE
  ${AIRINK_SRC}/app.cpp
  ${AIRINK_SRC}/menu.cpp
  ${AIRINK_SRC}/input/button.cpp
  ${AIRINK_SRC}/sensors/scd41.cpp
  ${AIRINK_SRC}/sensors/battery.cpp
  ${AIRINK_SRC}/ui/display_ui.cpp
  ${AIRINK_SRC}/ui/ui_platform.cpp
  # UI fonts (1bpp), generated via lv_font_conv: B612 for text, DSEG7 for values.
  ${AIRINK_SRC}/fonts/b612_48.c
  ${AIRINK_SRC}/fonts/b612_28.c
  ${AIRINK_SRC}/fonts/b612_16.c
  ${AIRINK_SRC}/fonts/b612_14.c
  ${AIRINK_SRC}/fonts/dseg7_48.c
  ${AIRINK_SRC}/fonts/dseg7_18.c
  # Vendored SSD16xx driver with a custom solomon,ssd1683 quirk (max 400x400, panel_gates=300)
  # for the GDEY042T81/SSD1683. Paired with CONFIG_SSD16XX=n.
  ${AIRINK_SRC}/drivers/ssd16xx.c
)

ncs_configure_data_model()
# NORDIC SDK APP END
