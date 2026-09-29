// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui_textinput.h"

#include <string.h>

#include "lvgl.h"
#include "ui_common.h"

// The row, in order. Symbols start with the ones passwords and hostnames use
// most, then the rest of printable ASCII, so every WPA passphrase can be typed.
static const char *const SECTIONS[] = {
    "abcdefghijklmnopqrstuvwxyz",
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ",
    "0123456789",
    ".-_@!#$%&*+=?/:~^,;()[]{}<>'\"`|\\",
};
static const char *const SECTION_NAMES[] = { "abc", "ABC", "123", "#+=" };
#define N_SECTIONS (sizeof(SECTIONS) / sizeof(SECTIONS[0]))

#define CELL_W 38
#define CELL_H 50

typedef struct {
    ui_text_input_cfg_t cfg;
    lv_obj_t *ta;
    lv_obj_t *title;
    char      title_text[48];     // to restore after an error message
    bool      showing_error;
    lv_obj_t *strip;
    lv_obj_t *eye_label;
    lv_obj_t *section_start[N_SECTIONS];   // first cell of each section
    lv_obj_t *tab[N_SECTIONS];
} input_t;

// The entry state hangs off the screen that owns the widget being handled.
static input_t *get(lv_event_t *e)
{
    return lv_obj_get_user_data(lv_obj_get_screen(lv_event_get_current_target(e)));
}

static void on_screen_deleted(lv_event_t *e)
{
    lv_free(lv_event_get_user_data(e));
}

// Tap on any cell of the row (events bubble up from the cell labels).
static void on_strip_click(lv_event_t *e)
{
    input_t *in = get(e);
    lv_obj_t *cell = lv_event_get_target(e);
    if (cell == in->strip) return;
    const char *txt = lv_label_get_text(cell);
    lv_textarea_add_char(in->ta, (uint32_t)(unsigned char)txt[0]);
}

// Highlight the jump button of the section currently in the middle of the row.
static void on_strip_scroll(lv_event_t *e)
{
    input_t *in = get(e);
    const int32_t mid = lv_obj_get_scroll_x(in->strip) + lv_obj_get_width(in->strip) / 2;
    size_t cur = 0;
    for (size_t i = 0; i < N_SECTIONS; i++) {
        if (lv_obj_get_x(in->section_start[i]) <= mid) cur = i;
    }
    for (size_t i = 0; i < N_SECTIONS; i++) {
        if (i == cur) lv_obj_add_state(in->tab[i], LV_STATE_CHECKED);
        else          lv_obj_remove_state(in->tab[i], LV_STATE_CHECKED);
    }
}

static void on_tab(lv_event_t *e)
{
    input_t *in = get(e);
    const size_t i = (size_t)(uintptr_t)lv_event_get_user_data(e);
    lv_obj_scroll_to_x(in->strip, lv_obj_get_x(in->section_start[i]), LV_ANIM_ON);
}

static void on_backspace(lv_event_t *e) { lv_textarea_delete_char(get(e)->ta); }
static void on_space(lv_event_t *e)     { lv_textarea_add_char(get(e)->ta, ' '); }

static void on_toggle_visible(lv_event_t *e)
{
    input_t *in = get(e);
    const bool hidden = !lv_textarea_get_password_mode(in->ta);
    lv_textarea_set_password_mode(in->ta, hidden);
    lv_label_set_text(in->eye_label, hidden ? LV_SYMBOL_EYE_OPEN : LV_SYMBOL_EYE_CLOSE);
}

static void on_ok(lv_event_t *e)
{
    input_t *in = get(e);
    const char *error = NULL;
    if (!in->cfg.on_done(lv_textarea_get_text(in->ta), in->cfg.ctx, &error)) {
        lv_obj_set_style_text_color(in->title, COL_DANGER, 0);
        lv_label_set_text(in->title, error ? error : "Invalid value");
        in->showing_error = true;
    }
}

// Any edit clears a previous error message.
static void on_text_changed(lv_event_t *e)
{
    input_t *in = get(e);
    if (!in->showing_error) return;
    in->showing_error = false;
    lv_obj_set_style_text_color(in->title, COL_TEXT, 0);
    lv_label_set_text(in->title, in->title_text);
}

static void on_back(lv_event_t *e)
{
    input_t *in = get(e);
    if (in->cfg.on_cancel) in->cfg.on_cancel(in->cfg.ctx);
    else                   ui_dashboard_show();
}

// A transparent full-width row container.
static lv_obj_t *row_create(lv_obj_t *parent, int32_t h)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, lv_pct(100), h);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 6, 0);
    return row;
}

static void build_strip(input_t *in, lv_obj_t *scr)
{
    lv_obj_t *strip = row_create(scr, CELL_H);
    in->strip = strip;
    lv_obj_set_style_pad_column(strip, 4, 0);
    lv_obj_set_scroll_dir(strip, LV_DIR_HOR);
    lv_obj_set_scrollbar_mode(strip, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_event_cb(strip, on_strip_click, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(strip, on_strip_scroll, LV_EVENT_SCROLL, NULL);

    for (size_t s = 0; s < N_SECTIONS; s++) {
        for (const char *c = SECTIONS[s]; *c; c++) {
            // A label as the cell keeps this to one object per character.
            lv_obj_t *cell = lv_label_create(strip);
            const char txt[2] = { *c, '\0' };
            lv_label_set_text(cell, txt);
            lv_obj_set_size(cell, CELL_W, CELL_H);
            lv_obj_set_style_text_font(cell, &noto_sans_bold_20, 0);
            lv_obj_set_style_text_align(cell, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_set_style_pad_top(cell, 12, 0);
            lv_obj_set_style_radius(cell, 6, 0);
            lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, 0);
            lv_obj_set_style_bg_color(cell, COL_CARD, 0);
            lv_obj_set_style_bg_color(cell, COL_ACCENT, LV_STATE_PRESSED);
            lv_obj_set_clickable(cell, true);
            lv_obj_set_event_bubble(cell, true);
            if (c == SECTIONS[s]) in->section_start[s] = cell;
        }
    }
}

void ui_text_input_open(const ui_text_input_cfg_t *cfg)
{
    input_t *in = lv_malloc_zeroed(sizeof(*in));
    in->cfg = *cfg;
    // cfg's strings may be the caller's stack buffers: keep our own title and
    // drop the rest, which is only needed while building the screen.
    lv_snprintf(in->title_text, sizeof(in->title_text), "%s", cfg->title);
    in->cfg.title = in->title_text;
    in->cfg.initial = NULL;
    in->cfg.placeholder = NULL;

    lv_obj_t *scr = ui_screen_create();
    lv_obj_set_user_data(scr, in);
    lv_obj_add_event_cb(scr, on_screen_deleted, LV_EVENT_DELETE, in);

    // Header: back, title, show/hide for passwords.
    lv_obj_t *header = ui_header_create(scr, cfg->title, on_back, NULL);
    in->title = lv_obj_get_child(header, -1);
    if (cfg->password) {
        lv_obj_t *eye = ui_button_create(header, LV_SYMBOL_EYE_OPEN, lv_color_hex(0),
                                         on_toggle_visible, NULL);
        lv_obj_set_size(eye, 44, 32);
        in->eye_label = lv_obj_get_child(eye, 0);
    }

    // The text being edited. Tapping it moves the cursor.
    in->ta = lv_textarea_create(scr);
    lv_obj_set_size(in->ta, lv_pct(100), 38);
    lv_textarea_set_one_line(in->ta, true);
    lv_textarea_set_max_length(in->ta, cfg->max_len);
    lv_textarea_set_password_mode(in->ta, cfg->password);
    lv_textarea_set_text(in->ta, cfg->initial ? cfg->initial : "");
    if (cfg->placeholder) lv_textarea_set_placeholder_text(in->ta, cfg->placeholder);
    lv_obj_set_style_text_font(in->ta, &noto_sans_bold_20, 0);
    lv_obj_set_style_bg_color(in->ta, COL_CARD, 0);
    lv_obj_set_style_border_color(in->ta, COL_ACCENT, 0);
    lv_obj_set_style_pad_ver(in->ta, 6, 0);
    lv_obj_add_state(in->ta, LV_STATE_FOCUSED);   // show the cursor
    lv_obj_add_event_cb(in->ta, on_text_changed, LV_EVENT_VALUE_CHANGED, NULL);

    // Jump buttons: scroll the row to a section.
    lv_obj_t *tabs = row_create(scr, 28);
    for (size_t i = 0; i < N_SECTIONS; i++) {
        in->tab[i] = ui_button_create(tabs, SECTION_NAMES[i], COL_CARD, on_tab,
                                      (void *)(uintptr_t)i);
        lv_obj_set_flex_grow(in->tab[i], 1);
        lv_obj_set_height(in->tab[i], 28);
        lv_obj_set_style_pad_ver(in->tab[i], 2, 0);
        lv_obj_set_style_bg_color(in->tab[i], COL_ACCENT, LV_STATE_CHECKED);
    }

    build_strip(in, scr);

    // Delete, space, OK.
    lv_obj_t *ctl = row_create(scr, 38);
    lv_obj_t *del = ui_button_create(ctl, LV_SYMBOL_BACKSPACE, lv_color_hex(0),
                                     on_backspace, NULL);
    lv_obj_set_size(del, 64, 36);
    lv_obj_t *space = ui_button_create(ctl, "Space", lv_color_hex(0), on_space, NULL);
    lv_obj_set_height(space, 36);
    lv_obj_set_flex_grow(space, 1);
    lv_obj_t *ok = ui_button_create(ctl, cfg->ok_label ? cfg->ok_label : "OK",
                                    COL_ACCENT, on_ok, NULL);
    lv_obj_set_size(ok, 96, 36);

    ui_screen_show(scr);
    lv_obj_add_state(in->tab[0], LV_STATE_CHECKED);
}
