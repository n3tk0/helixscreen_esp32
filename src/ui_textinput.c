// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui_textinput.h"

#include <string.h>

#include "lvgl.h"
#include "ui_common.h"

// The characters, one group per filter. Letters are lowercase only: holding a
// letter types its capital. Symbols start with the ones passwords and
// hostnames use most, then the rest of printable ASCII, so every WPA
// passphrase can be typed.
typedef enum { GROUP_LETTERS, GROUP_NUMBERS, GROUP_SYMBOLS, N_GROUPS } group_t;

static const struct {
    const char *chars;
    const char *short_name;   // on the filter button
    const char *long_name;    // in the pop-up
} GROUPS[N_GROUPS] = {
    [GROUP_LETTERS] = { "abcdefghijklmnopqrstuvwxyz", "abc", "Letters" },
    [GROUP_NUMBERS] = { "0123456789", "123", "Numbers" },
    [GROUP_SYMBOLS] = { ".-_@!#$%&*+=?/:~^,;()[]{}<>'\"`|\\", "#+=", "Symbols" },
};

#define CELL_W 38
#define ROW_H  58

typedef struct {
    ui_text_input_cfg_t cfg;
    lv_obj_t *ta;
    lv_obj_t *title;
    char      title_text[48];     // to restore after an error message
    bool      showing_error;
    lv_obj_t *strip;
    lv_obj_t *filter_label;
    lv_obj_t *hint;
    lv_obj_t *popup;              // filter pop-up while open, else NULL
    group_t   group;
    lv_obj_t *eye_label;
} input_t;

// The entry state hangs off the screen that owns the widget being handled.
static input_t *get(lv_event_t *e)
{
    return lv_obj_get_user_data(lv_obj_get_screen(lv_event_get_current_target(e)));
}

static void on_screen_deleted(lv_event_t *e)
{
    input_t *in = lv_event_get_user_data(e);
    if (in->popup) lv_obj_delete(in->popup);   // lives on the top layer
    lv_free(in);
}

static bool is_letter(char c) { return c >= 'a' && c <= 'z'; }

// The cell a strip event came from, or NULL for the strip itself.
static lv_obj_t *event_cell(lv_event_t *e, input_t *in)
{
    lv_obj_t *cell = lv_event_get_target(e);
    return cell == in->strip ? NULL : cell;
}

// Tap: type the character shown. Not sent after a hold or a swipe.
static void on_cell_tap(lv_event_t *e)
{
    input_t *in = get(e);
    lv_obj_t *cell = event_cell(e, in);
    if (!cell) return;
    lv_textarea_add_char(in->ta, (uint32_t)(unsigned char)lv_label_get_text(cell)[0]);
}

// Hold (400 ms, finger not moving): a letter types its capital, which the
// cell shows until release. Other characters type themselves.
static void on_cell_hold(lv_event_t *e)
{
    input_t *in = get(e);
    lv_obj_t *cell = event_cell(e, in);
    if (!cell) return;
    char c = lv_label_get_text(cell)[0];
    if (is_letter(c)) {
        c = (char)(c - 'a' + 'A');
        const char up[2] = { c, '\0' };
        lv_label_set_text(cell, up);
    }
    lv_textarea_add_char(in->ta, (uint32_t)(unsigned char)c);
}

static void on_cell_release(lv_event_t *e)
{
    input_t *in = get(e);
    lv_obj_t *cell = event_cell(e, in);
    if (!cell) return;
    const char c = lv_label_get_text(cell)[0];
    if (c >= 'A' && c <= 'Z') {
        const char low[2] = { (char)(c - 'A' + 'a'), '\0' };
        lv_label_set_text(cell, low);
    }
}

// Show only the cells of `g`, from the start of the row.
static void apply_group(input_t *in, group_t g)
{
    in->group = g;
    const uint32_t n = lv_obj_get_child_count(in->strip);
    for (uint32_t i = 0; i < n; i++) {
        lv_obj_t *cell = lv_obj_get_child(in->strip, (int32_t)i);
        const char c = lv_label_get_text(cell)[0];
        lv_obj_set_hidden(cell, strchr(GROUPS[g].chars, c) == NULL);
    }
    lv_obj_scroll_to_x(in->strip, 0, LV_ANIM_OFF);
    lv_label_set_text_fmt(in->filter_label, "%s\n" LV_SYMBOL_DOWN, GROUPS[g].short_name);
    lv_label_set_text(in->hint, g == GROUP_LETTERS ? "Hold a letter for a capital" : "");
}

// --- Filter pop-up ---------------------------------------------------------

static void on_popup_deleted(lv_event_t *e)
{
    input_t *in = lv_event_get_user_data(e);
    in->popup = NULL;
}

static void close_popup(input_t *in)
{
    if (in->popup) lv_obj_delete_async(in->popup);
}

static void on_popup_backdrop(lv_event_t *e)
{
    // Only taps on the dimmed area close it, not taps inside the panel.
    if (lv_event_get_target(e) != lv_event_get_current_target(e)) return;
    close_popup(lv_event_get_user_data(e));
}

static void on_popup_option(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_current_target(e);
    input_t *in = lv_obj_get_user_data(lv_obj_get_parent(lv_obj_get_parent(btn)));
    apply_group(in, (group_t)(uintptr_t)lv_event_get_user_data(e));
    close_popup(in);
}

static void on_filter(lv_event_t *e)
{
    input_t *in = get(e);
    if (in->popup) return;
    lv_obj_t *filter_btn = lv_event_get_current_target(e);

    // A dimmed full-screen backdrop on the top layer; tapping it cancels.
    lv_obj_t *bg = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(bg);
    lv_obj_set_size(bg, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(bg, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(bg, LV_OPA_50, 0);
    lv_obj_set_clickable(bg, true);
    lv_obj_set_user_data(bg, in);
    lv_obj_add_event_cb(bg, on_popup_backdrop, LV_EVENT_CLICKED, in);
    lv_obj_add_event_cb(bg, on_popup_deleted, LV_EVENT_DELETE, in);
    in->popup = bg;

    lv_obj_t *panel = lv_obj_create(bg);
    lv_obj_set_size(panel, 170, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(panel, COL_CARD, 0);
    lv_obj_set_style_border_color(panel, COL_BORDER, 0);
    lv_obj_set_style_radius(panel, 8, 0);
    lv_obj_set_style_pad_all(panel, 6, 0);
    lv_obj_set_style_pad_row(panel, 6, 0);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollable(panel, false);
    lv_obj_set_style_text_font(panel, &noto_sans_14, 0);

    for (int g = 0; g < N_GROUPS; g++) {
        char text[32];
        lv_snprintf(text, sizeof(text), "%s    %s", GROUPS[g].short_name, GROUPS[g].long_name);
        lv_obj_t *opt = ui_button_create(panel, text,
                                         g == (int)in->group ? COL_ACCENT : lv_color_hex(0),
                                         on_popup_option, (void *)(uintptr_t)g);
        lv_obj_set_size(opt, lv_pct(100), 38);
        lv_obj_align(lv_obj_get_child(opt, 0), LV_ALIGN_LEFT_MID, 0, 0);
    }

    // Beside the filter button, over the character row.
    lv_obj_update_layout(panel);
    lv_obj_align_to(panel, filter_btn, LV_ALIGN_OUT_RIGHT_MID, 6, 0);
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

// The filter button and the scrolling character row beside it.
static void build_keys(input_t *in, lv_obj_t *scr)
{
    lv_obj_t *row = row_create(scr, ROW_H);

    lv_obj_t *filter = ui_button_create(row, "", lv_color_hex(0), on_filter, NULL);
    lv_obj_set_size(filter, 50, ROW_H);
    lv_obj_set_style_pad_hor(filter, 2, 0);
    in->filter_label = lv_obj_get_child(filter, 0);
    lv_obj_set_style_text_align(in->filter_label, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *strip = row_create(row, ROW_H);
    in->strip = strip;
    lv_obj_set_width(strip, 0);
    lv_obj_set_flex_grow(strip, 1);
    lv_obj_set_style_pad_column(strip, 4, 0);
    lv_obj_set_scroll_dir(strip, LV_DIR_HOR);
    lv_obj_set_scrollbar_mode(strip, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_event_cb(strip, on_cell_tap, LV_EVENT_SHORT_CLICKED, NULL);
    lv_obj_add_event_cb(strip, on_cell_hold, LV_EVENT_LONG_PRESSED, NULL);
    lv_obj_add_event_cb(strip, on_cell_release, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(strip, on_cell_release, LV_EVENT_PRESS_LOST, NULL);

    const int32_t pad_top = (ROW_H - lv_font_get_line_height(&noto_sans_bold_20)) / 2;
    for (int g = 0; g < N_GROUPS; g++) {
        for (const char *c = GROUPS[g].chars; *c; c++) {
            // A label as the cell keeps this to one object per character.
            lv_obj_t *cell = lv_label_create(strip);
            const char txt[2] = { *c, '\0' };
            lv_label_set_text(cell, txt);
            lv_obj_set_size(cell, CELL_W, ROW_H);
            lv_obj_set_style_text_font(cell, &noto_sans_bold_20, 0);
            lv_obj_set_style_text_align(cell, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_set_style_pad_top(cell, pad_top, 0);
            lv_obj_set_style_radius(cell, 6, 0);
            lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, 0);
            lv_obj_set_style_bg_color(cell, COL_CARD, 0);
            lv_obj_set_style_bg_color(cell, COL_ACCENT, LV_STATE_PRESSED);
            lv_obj_set_clickable(cell, true);
            lv_obj_set_event_bubble(cell, true);
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

    in->hint = lv_label_create(scr);
    lv_obj_set_style_text_color(in->hint, COL_MUTED, 0);
    lv_obj_set_height(in->hint, lv_font_get_line_height(&noto_sans_14));

    build_keys(in, scr);
    apply_group(in, GROUP_LETTERS);

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
}
