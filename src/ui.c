// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui.h"

#include <stdio.h>
#include <string.h>

#include "lvgl.h"
#include "moonraker_client.h"
#include "settings.h"
#include "ui_common.h"
#include "ui_settings.h"
#include "wifi.h"

static lv_obj_t *s_dashboard;
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

static void on_settings(lv_event_t *e)
{
    (void)e;
    ui_settings_open();
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
    lv_obj_set_scrollable(card, false);
    return card;
}

static lv_obj_t *make_temp_card(lv_obj_t *parent, const char *title)
{
    lv_obj_t *card = make_card(parent);
    lv_obj_set_size(card, 148, 68);
    lv_obj_set_style_pad_all(card, 8, 0);
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

    // Say which link is down, so a blank dashboard is never a mystery.
    wifi_status_t ws;
    wifi_get_status(&ws);
    if (ws.state == WIFI_ST_NOT_CONFIGURED) {
        lv_label_set_text(s_lbl_status, "WiFi not set up");
    } else if (ws.state == WIFI_ST_FAILED) {
        lv_label_set_text_fmt(s_lbl_status, "WiFi: %s", ws.error ? ws.error : "offline");
    } else if (ws.state != WIFI_ST_CONNECTED) {
        lv_label_set_text(s_lbl_status, "WiFi connecting...");
    } else if (!st.connected) {
        lv_label_set_text(s_lbl_status, "Printer connecting...");
    } else {
        lv_label_set_text(s_lbl_status, st.print_state);
    }
    lv_obj_set_style_text_color(s_lbl_status,
                                ws.state == WIFI_ST_FAILED ? COL_DANGER
                                : st.connected ? COL_ACCENT : COL_MUTED, 0);

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

lv_obj_t *ui_dashboard_screen(void)
{
    return s_dashboard;
}

void ui_create(void)
{
    // LVGL's default theme is light: it would paint text dark inside cards and
    // text fields, invisible on our dark background. Use its dark variant,
    // with the Helix accent colour for buttons and focus.
    lv_display_t *disp = lv_display_get_default();
    lv_display_set_theme(disp, lv_theme_default_init(disp, COL_ACCENT, COL_SUCCESS,
                                                     true, &noto_sans_14));

    lv_obj_t *scr = ui_screen_create();
    s_dashboard = scr;
    lv_obj_set_flex_align(scr, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // --- Header: status + settings ---
    lv_obj_t *header = lv_obj_create(scr);
    lv_obj_remove_style_all(header);
    lv_obj_set_size(header, lv_pct(100), 32);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    s_lbl_status = lv_label_create(header);
    lv_obj_set_flex_grow(s_lbl_status, 1);
    lv_label_set_long_mode(s_lbl_status, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(s_lbl_status, COL_ACCENT, 0);
    lv_obj_set_style_text_font(s_lbl_status, &noto_sans_bold_20, 0);
    lv_label_set_text(s_lbl_status, "starting");

    lv_obj_t *gear = ui_button_create(header, LV_SYMBOL_SETTINGS, lv_color_hex(0),
                                      on_settings, NULL);
    lv_obj_set_size(gear, 44, 32);

    // --- Temp row: two cards ---
    lv_obj_t *row = lv_obj_create(scr);
    lv_obj_set_size(row, lv_pct(100), 70);
    lv_obj_set_style_bg_opa(row, LV_OPA_0, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollable(row, false);

    s_lbl_nozzle = make_temp_card(row, "Nozzle");
    s_lbl_bed    = make_temp_card(row, "Bed");

    // --- Print file + percentage on one line, bar below ---
    lv_obj_t *file_row = lv_obj_create(scr);
    lv_obj_remove_style_all(file_row);
    lv_obj_set_size(file_row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(file_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(file_row, 8, 0);

    s_lbl_file = lv_label_create(file_row);
    lv_obj_set_style_text_color(s_lbl_file, COL_MUTED, 0);
    lv_label_set_long_mode(s_lbl_file, LV_LABEL_LONG_DOT);
    lv_obj_set_flex_grow(s_lbl_file, 1);
    lv_label_set_text(s_lbl_file, "(no file)");

    s_lbl_progress = lv_label_create(file_row);
    lv_obj_set_style_text_color(s_lbl_progress, COL_TEXT, 0);
    lv_label_set_text(s_lbl_progress, "0%");

    s_bar_progress = lv_bar_create(scr);
    lv_obj_set_size(s_bar_progress, lv_pct(100), 14);
    lv_obj_set_style_bg_color(s_bar_progress, COL_CARD, 0);
    lv_obj_set_style_bg_color(s_bar_progress, COL_ACCENT, LV_PART_INDICATOR);
    lv_bar_set_range(s_bar_progress, 0, 100);

    // --- Control buttons ---
    lv_obj_t *btn_row = lv_obj_create(scr);
    lv_obj_set_size(btn_row, lv_pct(100), 44);
    lv_obj_set_style_bg_opa(btn_row, LV_OPA_0, 0);
    lv_obj_set_style_border_width(btn_row, 0, 0);
    lv_obj_set_style_pad_all(btn_row, 0, 0);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollable(btn_row, false);

    lv_obj_t *btn_pr = ui_button_create(btn_row, "Pause / Resume", COL_ACCENT,
                                        on_pause_resume, NULL);
    lv_obj_set_height(btn_pr, 40);
    lv_obj_t *btn_stop = ui_button_create(btn_row, LV_SYMBOL_STOP " E-STOP", COL_DANGER,
                                          on_estop, NULL);
    lv_obj_set_height(btn_stop, 40);

    // Refresh widgets twice a second from the live snapshot.
    lv_timer_create(refresh_cb, 500, NULL);

    lv_screen_load(scr);

    // First boot with no WiFi saved: go straight to network setup.
    helix_settings_t cfg;
    settings_get(&cfg);
    if (cfg.ssid[0] == '\0') ui_wifi_setup_open();
}
