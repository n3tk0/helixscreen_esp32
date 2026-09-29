// SPDX-License-Identifier: GPL-3.0-or-later
//
// CST328 capacitive touch for the Waveshare ESP32-S3-Touch-LCD-2.8.
#pragma once

#include <stdbool.h>

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// Bring up the touch controller on its dedicated I2C bus and register it as an
// LVGL pointer input on `disp`. Call with LVGL initialized and the display
// created. Returns false if the controller doesn't respond.
bool touch_init(lv_display_t *disp);

#ifdef __cplusplus
}
#endif
