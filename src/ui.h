// SPDX-License-Identifier: GPL-3.0-or-later
//
// LVGL dashboard. Built once; refreshed from the Moonraker snapshot on a timer.
#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// Build and show the dashboard; opens WiFi setup instead if nothing is saved
// yet. Caller must hold the LVGL lock (display_lvgl_lock).
void ui_create(void);

// The dashboard screen, which stays alive while settings screens come and go.
lv_obj_t *ui_dashboard_screen(void);

#ifdef __cplusplus
}
#endif
