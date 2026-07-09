cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(AirInk)

# Which main() to build. A bench harness REPLACES the firmware's main.cpp; it is not a
# test suite (it needs the real hardware + a PPK2). Select it at configure time:
#   west build -b promicro_nrf52840/nrf52840 -p -- -DAPP_ENTRY=power_test
#
# Read with zephyr_get(), not a plain cache variable: under sysbuild the -D lands in
# the sysbuild cache, which is exposed to the image as target properties rather than
# its CMake cache. A `set(APP_ENTRY ... CACHE ...)` would silently keep the default.
zephyr_get(APP_ENTRY SYSBUILD GLOBAL)
if(NOT APP_ENTRY)
	set(APP_ENTRY main)
endif()
message(STATUS "AirInk entry point: ${APP_ENTRY}")

if(APP_ENTRY STREQUAL "main")
	# The button and the menu state machine belong to the firmware's main() only; a
	# bench harness drives the hardware directly and must not have an input callback
	# registered behind its back.
	target_sources(app PRIVATE
		src/main.cpp
		src/menu.cpp
		src/input/button.cpp
	)
elseif(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/bench/${APP_ENTRY}.cpp)
	target_sources(app PRIVATE bench/${APP_ENTRY}.cpp)
else()
	message(FATAL_ERROR "APP_ENTRY='${APP_ENTRY}': no such harness bench/${APP_ENTRY}.cpp")
endif()

# So bench/ resolves "sensors/..." and "ui/..." the way sources inside src/ do.
target_include_directories(app PRIVATE src)

target_sources(app PRIVATE
	src/sensors/scd41.cpp
	src/sensors/battery.cpp
	src/ui/display_ui.cpp
	src/ui/ui_platform.cpp
	# UI fonts (1bpp), generated via lv_font_conv: B612 for text, DSEG7 for values.
	src/fonts/b612_48.c
	src/fonts/b612_28.c
	src/fonts/b612_16.c
	src/fonts/b612_14.c
	src/fonts/dseg7_48.c
	src/fonts/dseg7_18.c
	# Vendored SSD16xx driver with a custom solomon,ssd1683 quirk (max 400x400,
	# panel_gates=300) for the GDEY042T81/SSD1683. Paired with CONFIG_SSD16XX=n.
	src/drivers/ssd16xx.c
)
