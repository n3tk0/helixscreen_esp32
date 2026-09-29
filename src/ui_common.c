// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui_common.h"

#include "ui.h"

lv_obj_t *ui_screen_create(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, COL_BG, 0);
    lv_obj_set_style_text_color(scr, COL_TEXT, 0);
    lv_obj_set_style_text_font(scr, &noto_sans_14, 0);
    lv_obj_set_style_pad_all(scr, 6, 0);
    lv_obj_set_style_pad_row(scr, 6, 0);
    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollable(scr, false);
    return scr;
}

void ui_screen_show(lv_obj_t *scr)
{
    lv_obj_t *old = lv_screen_active();
    if (old == scr) return;
    lv_screen_load(scr);
    // Deferred: we are usually inside an event handler of a widget on `old`.
    if (old != ui_dashboard_screen()) lv_obj_delete_async(old);
}

void ui_dashboard_show(void)
{
    ui_screen_show(ui_dashboard_screen());
}

static void on_owner_deleted(lv_event_t *e)
{
    lv_timer_delete((lv_timer_t *)lv_event_get_user_data(e));
}

void ui_bind_timer(lv_obj_t *obj, lv_timer_t *timer)
{
    lv_obj_add_event_cb(obj, on_owner_deleted, LV_EVENT_DELETE, timer);
}

lv_obj_t *ui_button_create(lv_obj_t *parent, const char *text, lv_color_t color,
                           lv_event_cb_t on_click, void *user_data)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_style_bg_color(btn, color.red || color.green || color.blue ? color : COL_ELEVATED, 0);
    lv_obj_set_style_radius(btn, 6, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_pad_hor(btn, 10, 0);
    lv_obj_set_style_pad_ver(btn, 6, 0);
    if (on_click) lv_obj_add_event_cb(btn, on_click, LV_EVENT_CLICKED, user_data);
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_obj_center(lbl);
    return btn;
}

lv_obj_t *ui_header_create(lv_obj_t *scr, const char *title, lv_event_cb_t on_back,
                           lv_obj_t **action_slot)
{
    lv_obj_t *row = lv_obj_create(scr);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, lv_pct(100), 34);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 8, 0);

    if (on_back) {
        lv_obj_t *back = ui_button_create(row, LV_SYMBOL_LEFT, lv_color_hex(0), on_back, NULL);
        lv_obj_set_size(back, 40, 32);
    }
    // One line, truncated with "...": a wrapped title would overflow the row.
    lv_obj_t *lbl = lv_label_create(row);
    lv_obj_set_style_text_font(lbl, &noto_sans_bold_20, 0);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);
    lv_obj_set_flex_grow(lbl, 1);
    lv_obj_set_height(lbl, lv_font_get_line_height(&noto_sans_bold_20));
    lv_label_set_text(lbl, title);

    if (action_slot) *action_slot = row;   // caller appends its button to the row
    return row;
}
