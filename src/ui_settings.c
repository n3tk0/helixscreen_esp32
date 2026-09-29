// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui_settings.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lvgl.h"
#include "moonraker_client.h"
#include "settings.h"
#include "ui_common.h"
#include "ui_textinput.h"
#include "wifi.h"

#define CONNECT_TIMEOUT_MS  30000
#define MAX_LISTED_APS      20

// Credentials being tried; saved only once the connection works.
static struct {
    char ssid[HELIX_SSID_MAX + 1];
    char pass[HELIX_PASS_MAX + 1];
} s_pending;

static void open_scan(bool rescan);
static void open_password(const char *ssid, const char *prefill);
static void start_connect(void);

static void on_back_to_dashboard(lv_event_t *e) { (void)e; ui_dashboard_show(); }
static void on_back_to_settings(lv_event_t *e)  { (void)e; ui_settings_open(); }
static void cancel_to_settings(void *ctx)       { (void)ctx; ui_settings_open(); }

// Signal strength as a percentage (-100 dBm -> 0 %, -50 dBm -> 100 %).
static int rssi_pct(int rssi)
{
    int p = 2 * (rssi + 100);
    return p < 0 ? 0 : p > 100 ? 100 : p;
}

// ===========================================================================
// Settings menu
// ===========================================================================

typedef struct {
    lv_obj_t *wifi_value;
    lv_obj_t *about_value;
} menu_t;

static void describe_wifi(char *buf, size_t cap)
{
    wifi_status_t st;
    wifi_get_status(&st);
    switch (st.state) {
    case WIFI_ST_NOT_CONFIGURED:
        snprintf(buf, cap, "Not set up");
        break;
    case WIFI_ST_CONNECTED:
        snprintf(buf, cap, "%s  ·  %d%%", st.ssid, rssi_pct(st.rssi));
        break;
    case WIFI_ST_FAILED:
        snprintf(buf, cap, "%s  ·  %s", st.ssid, st.error ? st.error : "not connected");
        break;
    default:
        snprintf(buf, cap, "%s  ·  connecting...", st.ssid);
        break;
    }
}

static void menu_refresh(lv_timer_t *t)
{
    menu_t *m = lv_timer_get_user_data(t);
    char buf[96];
    describe_wifi(buf, sizeof(buf));
    lv_label_set_text(m->wifi_value, buf);

    wifi_status_t st;
    wifi_get_status(&st);
    snprintf(buf, sizeof(buf), "%s  ·  IP %s", HELIX_FW_VERSION,
             st.ip[0] ? st.ip : "none");
    lv_label_set_text(m->about_value, buf);
}

// A one-line menu row: name on the left, muted value on the right.
// Returns the value label.
static lv_obj_t *menu_row(lv_obj_t *parent, const char *name, lv_event_cb_t cb)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, lv_pct(100), 46);
    lv_obj_set_style_bg_color(row, COL_CARD, 0);
    lv_obj_set_style_bg_color(row, COL_ELEVATED, LV_STATE_PRESSED);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_radius(row, 8, 0);
    lv_obj_set_style_pad_hor(row, 10, 0);
    lv_obj_set_style_pad_ver(row, 0, 0);
    lv_obj_set_style_pad_column(row, 10, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollable(row, false);
    if (cb) lv_obj_add_event_cb(row, cb, LV_EVENT_CLICKED, NULL);
    else    lv_obj_set_clickable(row, false);

    lv_obj_t *n = lv_label_create(row);
    lv_obj_set_style_text_font(n, &noto_sans_bold_20, 0);
    lv_label_set_text(n, name);

    lv_obj_t *v = lv_label_create(row);
    lv_obj_set_style_text_color(v, COL_MUTED, 0);
    lv_obj_set_style_text_align(v, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_long_mode(v, LV_LABEL_LONG_DOT);
    lv_obj_set_flex_grow(v, 1);
    lv_obj_set_height(v, lv_font_get_line_height(&noto_sans_14));
    lv_label_set_text(v, "");
    return v;
}

static void on_menu_wifi(lv_event_t *e) { (void)e; open_scan(true); }

static bool on_host_entered(const char *text, void *ctx, const char **error)
{
    (void)ctx;
    char host[HELIX_HOST_MAX + 1];
    snprintf(host, sizeof(host), "%s", text);
    unsigned long port = 7125;

    // "host" or "host:port". The part after the last ':' must be a number.
    char *colon = strrchr(host, ':');
    if (colon) {
        char *end;
        port = strtoul(colon + 1, &end, 10);
        if (colon[1] == '\0' || *end != '\0' || port == 0 || port > 65535) {
            *error = "Port must be 1-65535";
            return false;
        }
        *colon = '\0';
    }
    if (host[0] == '\0' || strchr(host, ' ')) {
        *error = "Enter a host name or IP";
        return false;
    }
    if (!settings_save_moonraker(host, (uint16_t)port)) {
        *error = "Could not save";
        return false;
    }
    moonraker_client_reconnect();
    ui_settings_open();
    return true;
}

static void open_host_entry(void)
{
    helix_settings_t cfg;
    settings_get(&cfg);
    char current[HELIX_HOST_MAX + 8];
    snprintf(current, sizeof(current), "%s:%u", cfg.host, (unsigned)cfg.port);

    const ui_text_input_cfg_t in = {
        .title     = "Printer address",
        .initial   = current,
        .max_len   = HELIX_HOST_MAX + 6,
        .ok_label  = "Save",
        .on_done   = on_host_entered,
        .on_cancel = cancel_to_settings,
    };
    ui_text_input_open(&in);
}

static void on_menu_printer(lv_event_t *e) { (void)e; open_host_entry(); }

static void on_menu_deleted(lv_event_t *e) { lv_free(lv_event_get_user_data(e)); }

void ui_settings_open(void)
{
    lv_obj_t *scr = ui_screen_create();
    ui_header_create(scr, LV_SYMBOL_SETTINGS "  Settings", on_back_to_dashboard, NULL);

    menu_t *m = lv_malloc_zeroed(sizeof(*m));
    lv_obj_add_event_cb(scr, on_menu_deleted, LV_EVENT_DELETE, m);

    lv_obj_t *list = lv_obj_create(scr);
    lv_obj_remove_style_all(list);
    lv_obj_set_width(list, lv_pct(100));
    lv_obj_set_flex_grow(list, 1);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list, 6, 0);

    m->wifi_value = menu_row(list, LV_SYMBOL_WIFI "  WiFi", on_menu_wifi);

    helix_settings_t cfg;
    settings_get(&cfg);
    lv_obj_t *printer = menu_row(list, LV_SYMBOL_HOME "  Printer", on_menu_printer);
    lv_label_set_text_fmt(printer, "%s:%u", cfg.host, (unsigned)cfg.port);

    m->about_value = menu_row(list, "About", NULL);

    lv_timer_t *t = lv_timer_create(menu_refresh, 1000, m);
    ui_bind_timer(scr, t);
    menu_refresh(t);

    ui_screen_show(scr);
}

// ===========================================================================
// WiFi network list
// ===========================================================================

typedef struct {
    lv_obj_t *status;
    lv_obj_t *list;
    bool      listed;   // results shown for the current scan
} scan_t;

static void on_network(lv_event_t *e)
{
    lv_obj_t *item = lv_event_get_current_target(e);
    const char *ssid = lv_label_get_text(lv_obj_get_child(item, 0));
    const bool secure = (bool)(uintptr_t)lv_event_get_user_data(e);

    helix_settings_t cfg;
    settings_get(&cfg);
    if (secure) {
        // Offer the saved password again when re-picking the saved network.
        open_password(ssid, strcmp(ssid, cfg.ssid) == 0 ? cfg.password : NULL);
    } else {
        snprintf(s_pending.ssid, sizeof(s_pending.ssid), "%s", ssid);
        s_pending.pass[0] = '\0';
        start_connect();
    }
}

static bool on_hidden_ssid(const char *text, void *ctx, const char **error)
{
    (void)ctx;
    if (text[0] == '\0') {
        *error = "Enter the network name";
        return false;
    }
    open_password(text, NULL);
    return true;
}

static void open_scan_cached(void *ctx) { (void)ctx; open_scan(false); }

static void on_other_network(lv_event_t *e)
{
    (void)e;
    const ui_text_input_cfg_t in = {
        .title       = "Other network",
        .placeholder = "Network name (SSID)",
        .max_len   = HELIX_SSID_MAX,
        .ok_label  = "Next",
        .on_done   = on_hidden_ssid,
        .on_cancel = open_scan_cached,
    };
    ui_text_input_open(&in);
}

static lv_obj_t *network_item(lv_obj_t *list, const char *left, const char *right)
{
    lv_obj_t *item = lv_obj_create(list);
    lv_obj_set_size(item, lv_pct(100), 40);
    lv_obj_set_style_bg_color(item, COL_CARD, 0);
    lv_obj_set_style_bg_color(item, COL_ELEVATED, LV_STATE_PRESSED);
    lv_obj_set_style_border_width(item, 0, 0);
    lv_obj_set_style_radius(item, 6, 0);
    lv_obj_set_style_pad_hor(item, 10, 0);
    lv_obj_set_style_pad_ver(item, 0, 0);
    lv_obj_set_flex_flow(item, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(item, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollable(item, false);

    lv_obj_t *name = lv_label_create(item);          // child 0: the SSID
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    lv_obj_set_flex_grow(name, 1);
    lv_obj_set_height(name, lv_font_get_line_height(&noto_sans_14));
    lv_label_set_text(name, left);

    if (right) {
        lv_obj_t *info = lv_label_create(item);
        lv_obj_set_style_text_color(info, COL_MUTED, 0);
        lv_label_set_text(info, right);
    }
    return item;
}

static void scan_poll(lv_timer_t *t)
{
    scan_t *s = lv_timer_get_user_data(t);
    if (s->listed) return;

    wifi_ap_t aps[MAX_LISTED_APS];
    const int n = wifi_scan_results(aps, MAX_LISTED_APS);
    if (n < 0) return;   // still scanning

    s->listed = true;
    lv_label_set_text(s->status, n ? "Tap a network" : "No networks found");

    helix_settings_t cfg;
    settings_get(&cfg);
    for (int i = 0; i < n; i++) {
        char info[24];
        snprintf(info, sizeof(info), "%s%d%%  %s",
                 strcmp(aps[i].ssid, cfg.ssid) == 0 ? LV_SYMBOL_OK "  " : "",
                 rssi_pct(aps[i].rssi), aps[i].secure ? "WPA" : "open");
        lv_obj_t *item = network_item(s->list, aps[i].ssid, info);
        lv_obj_add_event_cb(item, on_network, LV_EVENT_CLICKED,
                            (void *)(uintptr_t)aps[i].secure);
    }
    lv_obj_t *other = network_item(s->list, LV_SYMBOL_PLUS "  Other network...", NULL);
    lv_obj_add_event_cb(other, on_other_network, LV_EVENT_CLICKED, NULL);
}

static void on_rescan(lv_event_t *e)
{
    scan_t *s = lv_obj_get_user_data(lv_obj_get_screen(lv_event_get_current_target(e)));
    lv_obj_clean(s->list);
    lv_label_set_text(s->status, "Scanning...");
    s->listed = false;
    wifi_scan_start();
}

static void on_scan_deleted(lv_event_t *e) { lv_free(lv_event_get_user_data(e)); }

static void open_scan(bool rescan)
{
    lv_obj_t *scr = ui_screen_create();
    scan_t *s = lv_malloc_zeroed(sizeof(*s));
    lv_obj_set_user_data(scr, s);
    lv_obj_add_event_cb(scr, on_scan_deleted, LV_EVENT_DELETE, s);

    lv_obj_t *header;
    ui_header_create(scr, LV_SYMBOL_WIFI "  WiFi networks", on_back_to_settings, &header);
    lv_obj_t *refresh = ui_button_create(header, LV_SYMBOL_REFRESH, lv_color_hex(0),
                                         on_rescan, NULL);
    lv_obj_set_size(refresh, 44, 32);

    s->status = lv_label_create(scr);
    lv_obj_set_style_text_color(s->status, COL_MUTED, 0);
    lv_label_set_text(s->status, "Scanning...");

    s->list = lv_obj_create(scr);
    lv_obj_remove_style_all(s->list);
    lv_obj_set_width(s->list, lv_pct(100));
    lv_obj_set_flex_grow(s->list, 1);
    lv_obj_set_flex_flow(s->list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s->list, 4, 0);
    lv_obj_set_scroll_dir(s->list, LV_DIR_VER);

    if (rescan) wifi_scan_start();
    lv_timer_t *t = lv_timer_create(scan_poll, 300, s);
    ui_bind_timer(scr, t);

    ui_screen_show(scr);
}

void ui_wifi_setup_open(void)
{
    open_scan(true);
}

// ===========================================================================
// Password entry
// ===========================================================================

static bool on_password(const char *text, void *ctx, const char **error)
{
    (void)ctx;
    const size_t len = strlen(text);
    if (len < 8) {
        *error = "At least 8 characters";   // WPA2 minimum
        return false;
    }
    snprintf(s_pending.pass, sizeof(s_pending.pass), "%s", text);
    start_connect();
    return true;
}

static void open_password(const char *ssid, const char *prefill)
{
    // Both arguments may point into s_pending ("Try again"), so copy first.
    char ssid_copy[HELIX_SSID_MAX + 1];
    char pass_copy[HELIX_PASS_MAX + 1];
    snprintf(ssid_copy, sizeof(ssid_copy), "%s", ssid);
    snprintf(pass_copy, sizeof(pass_copy), "%s", prefill ? prefill : "");
    ssid = ssid_copy;
    prefill = pass_copy;
    snprintf(s_pending.ssid, sizeof(s_pending.ssid), "%s", ssid);
    char title[HELIX_SSID_MAX + 8];
    snprintf(title, sizeof(title), LV_SYMBOL_WIFI "  %s", ssid);

    const ui_text_input_cfg_t in = {
        .title       = title,
        .initial     = prefill,
        .placeholder = "Password",
        .max_len   = HELIX_PASS_MAX,
        .password  = true,
        .ok_label  = "Connect",
        .on_done   = on_password,
        .on_cancel = open_scan_cached,
    };
    ui_text_input_open(&in);
}

// ===========================================================================
// Connecting: try the credentials, save them only if they work
// ===========================================================================

typedef struct {
    lv_obj_t *spinner;
    lv_obj_t *msg;
    lv_obj_t *buttons;
    uint32_t  started;
    bool      done;
} connect_t;

static void on_try_again(lv_event_t *e)
{
    (void)e;
    open_password(s_pending.ssid, s_pending.pass);
}

static void on_connect_cancel(lv_event_t *e)
{
    (void)e;
    // Go back to the saved network (or to none, if nothing was saved yet).
    helix_settings_t cfg;
    settings_get(&cfg);
    wifi_connect_to(cfg.ssid, cfg.password);
    open_scan(false);
}

static void connect_finish(connect_t *c, bool ok, const char *text)
{
    c->done = true;
    lv_obj_set_hidden(c->spinner, true);
    lv_obj_set_style_text_color(c->msg, ok ? COL_SUCCESS : COL_DANGER, 0);
    lv_label_set_text(c->msg, text);
    lv_obj_clean(c->buttons);
    if (ok) {
        ui_button_create(c->buttons, "Done", COL_ACCENT, on_back_to_dashboard, NULL);
    } else {
        ui_button_create(c->buttons, "Cancel", lv_color_hex(0), on_connect_cancel, NULL);
        if (s_pending.pass[0]) {
            ui_button_create(c->buttons, "Try again", COL_ACCENT, on_try_again, NULL);
        }
    }
}

static void connect_poll(lv_timer_t *t)
{
    connect_t *c = lv_timer_get_user_data(t);
    if (c->done) return;

    wifi_status_t st;
    wifi_get_status(&st);
    char buf[96];
    if (st.state == WIFI_ST_CONNECTED && strcmp(st.ssid, s_pending.ssid) == 0) {
        if (settings_save_wifi(s_pending.ssid, s_pending.pass)) {
            snprintf(buf, sizeof(buf), LV_SYMBOL_OK "  Connected\nIP %s", st.ip);
            connect_finish(c, true, buf);
        } else {
            connect_finish(c, false, "Connected, but saving failed");
        }
    } else if (st.state == WIFI_ST_FAILED) {
        snprintf(buf, sizeof(buf), "%s", st.error ? st.error : "Could not connect");
        connect_finish(c, false, buf);
    } else if (lv_tick_elaps(c->started) > CONNECT_TIMEOUT_MS) {
        connect_finish(c, false, "Timed out");
    }
}

static void on_connect_deleted(lv_event_t *e) { lv_free(lv_event_get_user_data(e)); }

static void start_connect(void)
{
    wifi_connect_to(s_pending.ssid, s_pending.pass);

    lv_obj_t *scr = ui_screen_create();
    lv_obj_set_flex_align(scr, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    connect_t *c = lv_malloc_zeroed(sizeof(*c));
    lv_obj_add_event_cb(scr, on_connect_deleted, LV_EVENT_DELETE, c);
    c->started = lv_tick_get();

    lv_obj_t *title = lv_label_create(scr);
    lv_obj_set_style_text_font(title, &noto_sans_bold_20, 0);
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_set_width(title, lv_pct(100));
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(title, s_pending.ssid);

    c->spinner = lv_spinner_create(scr);
    lv_obj_set_size(c->spinner, 56, 56);
    lv_obj_set_style_arc_color(c->spinner, COL_ACCENT, LV_PART_INDICATOR);

    c->msg = lv_label_create(scr);
    lv_obj_set_style_text_align(c->msg, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(c->msg, "Connecting...");

    c->buttons = lv_obj_create(scr);
    lv_obj_remove_style_all(c->buttons);
    lv_obj_set_size(c->buttons, lv_pct(100), 40);
    lv_obj_set_flex_flow(c->buttons, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(c->buttons, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(c->buttons, 12, 0);
    ui_button_create(c->buttons, "Cancel", lv_color_hex(0), on_connect_cancel, NULL);

    lv_timer_t *t = lv_timer_create(connect_poll, 250, c);
    ui_bind_timer(scr, t);

    ui_screen_show(scr);
}
