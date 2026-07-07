#pragma once
#include <stdint.h>

/*
 * LVGL UI for the AirInk 4.2" 400x300 e-paper. Three screens, one visible at a
 * time; each show_*() updates its labels and pushes a full refresh to the panel.
 */
struct _lv_font_t;

namespace ui {

/* Build all screens and show the boot splash. Returns 0 on success. */
int init();

/* Override the sensor-screen fonts (big value / small unit / name). Used by the
 * host font-preview to A/B fonts; the firmware keeps the Montserrat defaults. */
void set_metric_fonts(const _lv_font_t *big, const _lv_font_t *unit,
		      const _lv_font_t *name);

void show_boot();
void show_reading(uint16_t co2_ppm, int32_t temp_c_x100, uint16_t hum_x100);
void show_error(const char *title, const char *detail);

} // namespace ui
