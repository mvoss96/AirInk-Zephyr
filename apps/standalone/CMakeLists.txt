cmake_minimum_required(VERSION 3.20.0)

set(AIRINK_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)

# The custom solomon,ssd1683 binding lives in <repo>/dts/bindings. Zephyr only looks under the
# application directory, which is this one -- point it at the repo root, or the e-paper node's
# width/height properties silently do not exist.
list(APPEND DTS_ROOT ${AIRINK_ROOT})

# The board's drivers, shared with the Matter build. Merged after prj.conf, so it holds board
# facts only -- see the file's header.
list(APPEND EXTRA_CONF_FILE ${AIRINK_ROOT}/conf/airink_hw.conf)

# The firmware version -- one file, <repo>/VERSION, for both apps and the panel. Must precede
# find_package(Zephyr): that is what loads version.cmake.
include(${AIRINK_ROOT}/cmake/airink_version.cmake)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(AirInk)

include(${AIRINK_ROOT}/cmake/airink_sources.cmake)

# Which main() to build. A bench harness REPLACES the firmware's main.cpp; it is not a test suite
# (it needs the real hardware + a PPK2). Select it at configure time:
#   west build -b promicro_nrf52840/nrf52840 apps/standalone -- -DAPP_ENTRY=power_test
#
# Read with zephyr_get(), not a plain cache variable: under sysbuild the -D lands in the sysbuild
# cache, which is exposed to the image as target properties rather than its CMake cache. A
# `set(APP_ENTRY ... CACHE ...)` would silently keep the default.
zephyr_get(APP_ENTRY SYSBUILD GLOBAL)
if(NOT APP_ENTRY)
	set(APP_ENTRY main)
endif()
message(STATUS "AirInk entry point: ${APP_ENTRY}")

if(APP_ENTRY STREQUAL "main")
	# The measurement loop, the menu and the button belong to the firmware's main() only; a bench
	# harness drives the hardware directly and must not have an input callback registered behind
	# its back.
	target_sources(app PRIVATE main.cpp ${AIRINK_LOOP_SOURCES})
elseif(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/bench/${APP_ENTRY}.cpp)
	target_sources(app PRIVATE bench/${APP_ENTRY}.cpp)
else()
	message(FATAL_ERROR "APP_ENTRY='${APP_ENTRY}': no such harness bench/${APP_ENTRY}.cpp")
endif()

# So main.cpp and bench/ resolve "sensors/..." and "ui/..." the way sources inside src/ do.
target_include_directories(app PRIVATE ${AIRINK_INCLUDE_DIR})

target_sources(app PRIVATE ${AIRINK_DEVICE_SOURCES})
