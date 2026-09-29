// SPDX-License-Identifier: GPL-3.0-or-later
//
// LVGL dashboard. Built once; refreshed from the Moonraker snapshot on a timer.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Build the widget tree on the active LVGL display. Caller must hold the LVGL
// lock (display_lvgl_lock).
void ui_create(void);

#ifdef __cplusplus
}
#endif
