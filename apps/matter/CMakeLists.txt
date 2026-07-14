#
# Copyright (c) 2025 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

cmake_minimum_required(VERSION 3.20.0)

set(AIRINK_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)

# The custom solomon,ssd1683 binding lives in <repo>/dts/bindings. Zephyr only looks under the
# application directory, which is this one -- point it at the repo root, or the e-paper node's
# width/height properties silently do not exist.
list(APPEND DTS_ROOT ${AIRINK_ROOT})

# The board's drivers, shared with the standalone firmware. Merged after prj.conf, so it holds
# board facts only -- see the file's header.
list(APPEND EXTRA_CONF_FILE ${AIRINK_ROOT}/conf/airink_hw.conf)

# conf/release.conf turns the logging off (worth ~7 uA). It is opt-in on the command line rather
# than a switch here, because EXTRA_CONF_FILE has to be set before find_package(Zephyr), and
# zephyr_get() -- the only thing that could read a sysbuild -D -- does not exist until after it:
#
#   west build -b promicro_nrf52840/nrf52840 apps/matter -d <dir> \
#       -- -Dmatter_EXTRA_CONF_FILE=<abs>/conf/release.conf
#
# The `matter_` prefix is how sysbuild routes a variable to this image rather than to itself; a bare
# -DAIRINK_POWER=y would land in the sysbuild cache and never be forwarded. See that file's header.

# The firmware version -- one file, <repo>/VERSION, for both apps and the panel. Must precede
# find_package(Zephyr): that is what loads version.cmake.
include(${AIRINK_ROOT}/cmake/airink_version.cmake)

find_package(Zephyr HINTS $ENV{ZEPHYR_BASE})

project(airink-matter)

# Enable GNU STD support.
include(${ZEPHYR_CONNECTEDHOMEIP_MODULE_DIR}/config/nrfconnect/app/enable-gnu-std.cmake)

# NORDIC SDK APP START

# Include all source files that are located in the Matter common directory.
include(${ZEPHYR_NRF_MODULE_DIR}/samples/matter/common/cmake/source_common.cmake)

# Include Data Model utils
include(${ZEPHYR_NRF_MODULE_DIR}/samples/matter/common/cmake/data_model.cmake)

include(${ZEPHYR_NRF_MODULE_DIR}/samples/matter/common/cmake/zap_helpers.cmake)
ncs_get_zap_parent_dir(ZAP_PARENT_DIR)

include(${AIRINK_ROOT}/cmake/airink_sources.cmake)

target_include_directories(app PRIVATE
  src
  ${ZAP_PARENT_DIR}
  ${AIRINK_INCLUDE_DIR}
)

# This build's own main(): it brings Matter up and then hands the device to app::run(), which runs
# on a thread of its own alongside the CHIP event loop and OpenThread.
target_sources(app PRIVATE
  src/app_task.cpp
  src/main.cpp
)

# The device itself -- the same sources the standalone firmware builds, minus its main().
target_sources(app PRIVATE
  ${AIRINK_DEVICE_SOURCES}
  ${AIRINK_LOOP_SOURCES}
)

ncs_configure_data_model()
# NORDIC SDK APP END
