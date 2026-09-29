// SPDX-License-Identifier: GPL-3.0-or-later
//
// Shared look and screen plumbing for the dashboard and settings screens.
#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// HelixScreen dark-theme design tokens, lifted verbatim from the parent
// project's default theme (assets/config/themes/defaults/helixscreen.json).
// We don't run the XML/token engine on the MCU, so the values are inlined.
#define COL_BG        lv_color_hex(0x19191C)  // screen_bg
#define COL_CARD      lv_color_hex(0x202023)  // card_bg
#define COL_ELEVATED  lv_color_hex(0x4A4A52)  // elevated_bg
#define COL_BORDER    lv_color_hex(0x36363C)  // border
#define COL_ACCENT    lv_color_hex(0x3A7CC8)  // primary
#define COL_TEXT      lv_color_hex(0xE8E8EC)  // text
#define COL_MUTED     lv_color_hex(0xB8B8C0)  // text_muted
#define COL_SUCCESS   lv_color_hex(0x5CB85C)  // success
#define COL_WARNING   lv_color_hex(0xE8A83A)  // warning
#define COL_DANGER    lv_color_hex(0xD94848)  // danger

// Helix typeface (Noto Sans), restored as LVGL font arrays. LVGL symbols
// (LV_SYMBOL_*) come from their Montserrat fallback.
LV_FONT_DECLARE(noto_sans_14);
LV_FONT_DECLARE(noto_sans_bold_20);

// A new, empty screen in the Helix style (not yet shown).
lv_obj_t *ui_screen_create(void);

// Show `scr`. The screen being left is deleted, unless it is the dashboard,
// which lives for the whole run.
void ui_screen_show(lv_obj_t *scr);

// Go back to the dashboard (deleting the current settings screen).
void ui_dashboard_show(void);

// Standard header row: optional back button, title, and room on the right for
// one action button (returned via *action_slot if non-NULL).
lv_obj_t *ui_header_create(lv_obj_t *scr, const char *title, lv_event_cb_t on_back,
                           lv_obj_t **action_slot);

// A button with a centred label. `color` of 0 means the theme default.
lv_obj_t *ui_button_create(lv_obj_t *parent, const char *text, lv_color_t color,
                           lv_event_cb_t on_click, void *user_data);

// Delete `timer` when `obj` is deleted, so screen-owned timers can't outlive
// their widgets.
void ui_bind_timer(lv_obj_t *obj, lv_timer_t *timer);

#ifdef __cplusplus
}
#endif
