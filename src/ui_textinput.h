// SPDX-License-Identifier: GPL-3.0-or-later
//
// Text entry without a full keyboard: one horizontally scrolling row with every
// letter, digit and symbol, plus jump buttons to each section of the row.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Called with the entered text when OK is tapped. Return true to accept (the
// callback normally shows the next screen); return false and set *error to
// keep the entry screen open and show that message.
typedef bool (*ui_text_done_cb)(const char *text, void *ctx, const char **error);
typedef void (*ui_text_cancel_cb)(void *ctx);

typedef struct {
    const char        *title;
    const char        *initial;    // pre-filled text, may be NULL
    const char        *placeholder;// hint shown while the field is empty, or NULL
    uint16_t           max_len;
    bool               password;   // hide characters (with a show/hide toggle)
    const char        *ok_label;   // e.g. "Connect"; NULL for "OK"
    ui_text_done_cb    on_done;
    ui_text_cancel_cb  on_cancel;  // back button; NULL just returns to dashboard
    void              *ctx;
} ui_text_input_cfg_t;

void ui_text_input_open(const ui_text_input_cfg_t *cfg);

#ifdef __cplusplus
}
#endif
