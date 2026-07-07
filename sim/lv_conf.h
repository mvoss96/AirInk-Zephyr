/*
 * Minimal lv_conf.h for the AirInk host preview build (LVGL 9.5).
 *
 * Only the settings that differ from LVGL's defaults are listed; lv_conf_internal.h
 * fills in everything else. Mirrors the on-target LVGL config from prj.conf:
 * monochrome look, Montserrat 14/16/28/48. We render at 8-bit grayscale on the
 * host (identical layout to the 1-bit panel) and threshold to pure B/W on export.
 */
#ifndef LV_CONF_H
#define LV_CONF_H

/* 8-bit grayscale render target (L8). Layout is independent of colour depth. */
#define LV_COLOR_DEPTH 8

/* Roomy heap on the host so Montserrat-48 rasterisation never runs dry. */
#define LV_USE_STDLIB_MALLOC  LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_STRING  LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_SPRINTF LV_STDLIB_BUILTIN
#define LV_MEM_SIZE (16 * 1024 * 1024)

#define LV_USE_OS LV_OS_NONE

/* No logging / asserts noise for the one-shot render. */
#define LV_USE_LOG 0

/* Fonts used by the UI (see prj.conf). */
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_MONTSERRAT_48 1
#define LV_FONT_DEFAULT &lv_font_montserrat_14

#endif /* LV_CONF_H */
