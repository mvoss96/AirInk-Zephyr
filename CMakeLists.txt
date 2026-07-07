cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(AirInk)

target_sources(app PRIVATE
	src/main.cpp
	src/display_ui.cpp
	src/ui_platform_zephyr.cpp
	# Vendored SSD16xx driver (ssd1680 quirk raised to 400x400 for the
	# GDEY042T81/SSD1683). Paired with CONFIG_SSD16XX=n.
	src/drivers/ssd16xx.c
)
