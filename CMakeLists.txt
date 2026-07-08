cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(AirInk)

target_sources(app PRIVATE
	src/main.cpp
	# Power/UI test harnesses — swap in (comment src/main.cpp) to use:
	#   src/partial_test.cpp   # 1 Hz counter, full then partial refresh
	#   src/power_test.cpp     # per-consumer isolation (SCD41/e-paper/idle), TEST_MODE
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
