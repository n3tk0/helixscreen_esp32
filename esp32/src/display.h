// SPDX-License-Identifier: GPL-3.0-or-later
//
// Display + LVGL bring-up for the Waveshare ESP32-S3-Touch-LCD-2.8.
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HELIX_LCD_H_RES 320
#define HELIX_LCD_V_RES 240

// Init the ST7789 panel over SPI and register an LVGL display. After this
// returns, LVGL is live and lv_timer_handler() may be pumped.
void display_init(void);

// LVGL is NOT thread-safe. Any task touching LVGL objects (the UI task, or a
// callback that mutates widgets) must hold this lock. The main loop that calls
// lv_timer_handler() takes it internally.
bool display_lvgl_lock(int timeout_ms);   // returns false on timeout
void display_lvgl_unlock(void);

#ifdef __cplusplus
}
#endif
