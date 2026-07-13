# The AirInk device, as source lists. Both applications (apps/standalone, apps/matter) include
# this file, for the same reason they share dts/airink_hw.dtsi and conf/airink_hw.conf: there is
# one device, and a file that only one of them compiles is a file the other silently lacks.
#
# Split in two, because a bench harness is not the firmware:
#
#   AIRINK_DEVICE_SOURCES  the sensors, the panel, the fonts, the vendored driver. Everything that
#                          talks to hardware. A PPK2 bench harness (apps/standalone/bench) drives
#                          these directly and brings its own main().
#   AIRINK_LOOP_SOURCES    the measurement loop, the menu and the button on top of them. The
#                          firmware -- both builds of it -- runs exactly this; a bench harness
#                          must NOT, or it would have an input callback registered behind its back.

set(AIRINK_ROOT ${CMAKE_CURRENT_LIST_DIR}/..)
set(AIRINK_INCLUDE_DIR ${AIRINK_ROOT}/src)

set(AIRINK_DEVICE_SOURCES
	${AIRINK_ROOT}/src/sensors/scd41.cpp
	${AIRINK_ROOT}/src/sensors/battery.cpp
	${AIRINK_ROOT}/src/ui/display_ui.cpp
	${AIRINK_ROOT}/src/ui/ui_platform.cpp
	# UI fonts (1bpp), generated via lv_font_conv: B612 for text, DSEG7 for values.
	${AIRINK_ROOT}/src/fonts/b612_48.c
	${AIRINK_ROOT}/src/fonts/b612_28.c
	${AIRINK_ROOT}/src/fonts/b612_16.c
	${AIRINK_ROOT}/src/fonts/b612_14.c
	${AIRINK_ROOT}/src/fonts/dseg7_48.c
	${AIRINK_ROOT}/src/fonts/dseg7_18.c
	# Vendored SSD16xx driver with a custom solomon,ssd1683 quirk (max 400x400, panel_gates=300)
	# for the GDEY042T81/SSD1683. Paired with CONFIG_SSD16XX=n.
	${AIRINK_ROOT}/src/drivers/ssd16xx.c
)

set(AIRINK_LOOP_SOURCES
	${AIRINK_ROOT}/src/app.cpp
	${AIRINK_ROOT}/src/menu.cpp
	${AIRINK_ROOT}/src/input/button.cpp
)
