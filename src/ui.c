// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui.h"

#include <stdio.h>
#include <string.h>

#include "lvgl.h"
#include "moonraker_client.h"

// HelixScreen dark-theme design tokens, lifted verbatim from the parent
// project's default theme (assets/config/themes/defaults/helixscreen.json).
// We don't run the XML/token engine on the MCU, so the values are inlined.
#define COL_BG        lv_color_hex(0x19191C)  // screen_bg
#define COL_CARD      lv_color_hex(0x202023)  // card_bg
#define COL_BORDER    lv_color_hex(0x36363C)  // border
#define COL_ACCENT    lv_color_hex(0x3A7CC8)  // primary
#define COL_TEXT      lv_color_hex(0xE8E8EC)  // text
#define COL_MUTED     lv_color_hex(0xB8B8C0)  // text_muted
#define COL_SUCCESS   lv_color_hex(0x5CB85C)  // success
#define COL_DANGER    lv_color_hex(0xD94848)  // danger

// Helix typeface (Noto Sans), restored as LVGL font arrays. Body text uses
// noto_sans_14; the status header + temperature values use the bold 20.
LV_FONT_DECLARE(noto_sans_14);
LV_FONT_DECLARE(noto_sans_bold_20);

static lv_obj_t *s_lbl_status;
static lv_obj_t *s_lbl_nozzle;
static lv_obj_t *s_lbl_bed;
static lv_obj_t *s_lbl_file;
static lv_obj_t *s_bar_progress;
static lv_obj_t *s_lbl_progress;

// ---------------------------------------------------------------------------
// Event callbacks — the declarative-UI desktop app forbids ad-hoc event_cb,
// but on the MCU skeleton we wire LVGL directly. Each just issues one RPC.
// ---------------------------------------------------------------------------

static void on_estop(lv_event_t *e)
{
    (void)e;
    moonraker_emergency_stop();
}

static void on_pause_resume(lv_event_t *e)
{
    (void)e;
    helix_printer_state_t st = {0};
    moonraker_get_state(&st);
    if (strcmp(st.print_state, "printing") == 0) {
        moonraker_print_pause();
    } else if (strcmp(st.print_state, "paused") == 0) {
        moonraker_print_resume();
    }
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static lv_obj_t *make_card(lv_obj_t *parent)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_style_bg_color(card, COL_CARD, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_radius(card, 10, 0);
    lv_obj_set_style_pad_all(card, 10, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    return card;
}

static lv_obj_t *make_temp_card(lv_obj_t *parent, const char *title)
{
    lv_obj_t *card = make_card(parent);
    lv_obj_set_size(card, 150, 78);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);

    lv_obj_t *t = lv_label_create(card);
    lv_label_set_text(t, title);
    lv_obj_set_style_text_color(t, COL_MUTED, 0);

    lv_obj_t *v = lv_label_create(card);
    lv_obj_set_style_text_color(v, COL_TEXT, 0);
    lv_obj_set_style_text_font(v, &noto_sans_bold_20, 0);
    lv_label_set_text(v, "--");
    return v;   // return the value label so the caller can update it
}

// ---------------------------------------------------------------------------
// Refresh timer — pulls the snapshot and updates labels. This is the embedded
// analogue of the desktop Subject→observer binding: one place reads state and
// writes widgets, on the LVGL task.
// ---------------------------------------------------------------------------

static void refresh_cb(lv_timer_t *timer)
{
    (void)timer;
    helix_printer_state_t st = {0};
    moonraker_get_state(&st);

    char buf[64];

    lv_label_set_text(s_lbl_status, st.connected ? st.print_state : "connecting...");
    lv_obj_set_style_text_color(s_lbl_status,
                                st.connected ? COL_ACCENT : COL_MUTED, 0);

    snprintf(buf, sizeof(buf), "%.0f / %.0f °C", st.nozzle_temp, st.nozzle_target);
    lv_label_set_text(s_lbl_nozzle, buf);

    snprintf(buf, sizeof(buf), "%.0f / %.0f °C", st.bed_temp, st.bed_target);
    lv_label_set_text(s_lbl_bed, buf);

    lv_label_set_text(s_lbl_file,
                      st.filename[0] ? st.filename : "(no file)");

    int pct = (int)(st.progress * 100.0f + 0.5f);
    lv_bar_set_value(s_bar_progress, pct, LV_ANIM_OFF);
    snprintf(buf, sizeof(buf), "%d%%", pct);
    lv_label_set_text(s_lbl_progress, buf);
}

// ---------------------------------------------------------------------------
// Build
// ---------------------------------------------------------------------------

void ui_create(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, COL_BG, 0);
    // Make the Helix body font the screen-wide default; children inherit it.
    lv_obj_set_style_text_font(scr, &noto_sans_14, 0);
    lv_obj_set_style_pad_all(scr, 8, 0);
    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(scr, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(scr, 8, 0);

    // --- Header: status ---
    s_lbl_status = lv_label_create(scr);
    lv_obj_set_style_text_color(s_lbl_status, COL_ACCENT, 0);
    lv_obj_set_style_text_font(s_lbl_status, &noto_sans_bold_20, 0);
    lv_label_set_text(s_lbl_status, "starting");

    // --- Temp row: two cards ---
    lv_obj_t *row = lv_obj_create(scr);
    lv_obj_set_size(row, lv_pct(100), 90);
    lv_obj_set_style_bg_opa(row, LV_OPA_0, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    s_lbl_nozzle = make_temp_card(row, "Nozzle");
    s_lbl_bed    = make_temp_card(row, "Bed");

    // --- Print file + progress ---
    s_lbl_file = lv_label_create(scr);
    lv_obj_set_style_text_color(s_lbl_file, COL_MUTED, 0);
    lv_label_set_long_mode(s_lbl_file, LV_LABEL_LONG_DOT);
    lv_obj_set_width(s_lbl_file, lv_pct(100));
    lv_label_set_text(s_lbl_file, "(no file)");

    s_bar_progress = lv_bar_create(scr);
    lv_obj_set_size(s_bar_progress, lv_pct(100), 14);
    lv_obj_set_style_bg_color(s_bar_progress, COL_CARD, 0);
    lv_obj_set_style_bg_color(s_bar_progress, COL_ACCENT, LV_PART_INDICATOR);
    lv_bar_set_range(s_bar_progress, 0, 100);

    s_lbl_progress = lv_label_create(scr);
    lv_obj_set_style_text_color(s_lbl_progress, COL_TEXT, 0);
    lv_label_set_text(s_lbl_progress, "0%");

    // --- Control buttons ---
    lv_obj_t *btn_row = lv_obj_create(scr);
    lv_obj_set_size(btn_row, lv_pct(100), 48);
    lv_obj_set_style_bg_opa(btn_row, LV_OPA_0, 0);
    lv_obj_set_style_border_width(btn_row, 0, 0);
    lv_obj_set_style_pad_all(btn_row, 0, 0);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(btn_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *btn_pr = lv_button_create(btn_row);
    lv_obj_add_event_cb(btn_pr, on_pause_resume, LV_EVENT_CLICKED, NULL);
    lv_obj_t *l1 = lv_label_create(btn_pr);
    lv_label_set_text(l1, "Pause / Resume");
    lv_obj_center(l1);

    lv_obj_t *btn_stop = lv_button_create(btn_row);
    lv_obj_set_style_bg_color(btn_stop, COL_DANGER, 0);
    lv_obj_add_event_cb(btn_stop, on_estop, LV_EVENT_CLICKED, NULL);
    lv_obj_t *l2 = lv_label_create(btn_stop);
    lv_label_set_text(l2, LV_SYMBOL_STOP " E-STOP");
    lv_obj_center(l2);

    // Refresh widgets twice a second from the live snapshot.
    lv_timer_create(refresh_cb, 500, NULL);
}
