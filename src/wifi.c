// SPDX-License-Identifier: GPL-3.0-or-later
#include "wifi.h"

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"

#include "settings.h"

static const char *TAG = "wifi";

#define MAX_SCAN_APS        20
#define FAILS_BEFORE_FAILED 3
#define RETRY_MIN_MS        2000
#define RETRY_MAX_MS        30000
#define SCAN_RETRY_MS       300
#define SCAN_MAX_RETRIES    10

// Everything below is shared by the event-loop task (wifi events), the
// esp_timer task (retries) and the LVGL task (the settings UI).
static SemaphoreHandle_t s_lock;
static char         s_ssid[33];
static char         s_pass[65];
static wifi_state_t s_state;
static const char  *s_error;
static char         s_ip[16];
static int          s_fails;
static bool         s_connected;
static bool         s_attempting;     // esp_wifi_connect() issued, no result yet
static bool         s_switch_pending; // we disconnected to apply new credentials
static bool         s_scanning;
static int          s_scan_tries;
static wifi_ap_t    s_aps[MAX_SCAN_APS];
static int          s_ap_count;

static esp_timer_handle_t s_retry_timer;
static esp_timer_handle_t s_scan_timer;

static void lock(void)   { xSemaphoreTake(s_lock, portMAX_DELAY); }
static void unlock(void) { xSemaphoreGive(s_lock); }

static const char *reason_text(uint8_t reason)
{
    switch (reason) {
    case WIFI_REASON_AUTH_FAIL:
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_MIC_FAILURE:
        return "Wrong password";
    case WIFI_REASON_NO_AP_FOUND:
    case 210: case 211: case 212:   // NO_AP_FOUND_W_COMPATIBLE_SECURITY etc. (IDF 5.2+)
        return "Network not found";
    default:
        return "Could not connect";
    }
}

// Issue a connect attempt with the current credentials. Caller holds the lock.
static void connect_locked(void)
{
    if (s_ssid[0] == '\0' || s_scanning) return;

    wifi_config_t cfg = { 0 };
    // Zero-initialized, so a full-length SSID/PSK without a NUL is fine.
    memcpy(cfg.sta.ssid, s_ssid, strnlen(s_ssid, sizeof(cfg.sta.ssid)));
    memcpy(cfg.sta.password, s_pass, strnlen(s_pass, sizeof(cfg.sta.password)));
    // Open networks need the security floor dropped, else the join is refused.
    cfg.sta.threshold.authmode = s_pass[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;

    esp_wifi_set_config(WIFI_IF_STA, &cfg);
    if (esp_wifi_connect() == ESP_OK) s_attempting = true;
}

static void schedule_retry_locked(void)
{
    int delay = RETRY_MIN_MS << (s_fails > 4 ? 4 : s_fails);
    if (delay > RETRY_MAX_MS) delay = RETRY_MAX_MS;
    esp_timer_stop(s_retry_timer);
    esp_timer_start_once(s_retry_timer, (uint64_t)delay * 1000);
}

static void retry_cb(void *arg)
{
    (void)arg;
    lock();
    if (!s_connected && !s_attempting) connect_locked();
    unlock();
}

static void try_scan_locked(void);

static void scan_retry_cb(void *arg)
{
    (void)arg;
    lock();
    try_scan_locked();
    unlock();
}

// esp_wifi_scan_start() is refused while a connect attempt is still winding
// down, so retry briefly instead of failing the scan.
static void try_scan_locked(void)
{
    if (!s_scanning) return;
    const wifi_scan_config_t cfg = { .show_hidden = false };
    esp_err_t err = esp_wifi_scan_start(&cfg, false);
    if (err == ESP_OK) return;
    if (++s_scan_tries < SCAN_MAX_RETRIES) {
        esp_timer_start_once(s_scan_timer, SCAN_RETRY_MS * 1000);
    } else {
        ESP_LOGE(TAG, "scan failed: %s", esp_err_to_name(err));
        s_scanning = false;
        s_ap_count = 0;
        connect_locked();
    }
}

// Collect results: skip hidden networks, keep the strongest entry per SSID,
// sort strongest first.
static void collect_scan_locked(void)
{
    uint16_t n = MAX_SCAN_APS;
    static wifi_ap_record_t recs[MAX_SCAN_APS];
    if (esp_wifi_scan_get_ap_records(&n, recs) != ESP_OK) n = 0;

    s_ap_count = 0;
    for (int i = 0; i < n; i++) {
        const char *ssid = (const char *)recs[i].ssid;
        if (ssid[0] == '\0') continue;
        int j = 0;
        while (j < s_ap_count && strcmp(s_aps[j].ssid, ssid) != 0) j++;
        if (j == s_ap_count) {
            s_ap_count++;
            snprintf(s_aps[j].ssid, sizeof(s_aps[j].ssid), "%s", ssid);
            s_aps[j].rssi = -128;
        }
        if (recs[i].rssi > s_aps[j].rssi) {
            s_aps[j].rssi   = recs[i].rssi;
            s_aps[j].secure = recs[i].authmode != WIFI_AUTH_OPEN;
        }
    }
    for (int i = 1; i < s_ap_count; i++) {       // insertion sort, n <= 20
        wifi_ap_t t = s_aps[i];
        int j = i - 1;
        while (j >= 0 && s_aps[j].rssi < t.rssi) { s_aps[j + 1] = s_aps[j]; j--; }
        s_aps[j + 1] = t;
    }
}

static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    lock();
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        connect_locked();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *d = data;
        s_attempting = false;
        s_connected  = false;
        if (s_switch_pending) {
            // Our own disconnect to change networks, not a failure.
            s_switch_pending = false;
            connect_locked();
        } else if (!s_scanning && s_ssid[0]) {
            s_error = reason_text(d->reason);
            // A rejected password won't fix itself, so report it at once.
            const bool bad_pass = strcmp(s_error, "Wrong password") == 0;
            if (++s_fails >= FAILS_BEFORE_FAILED || bad_pass) s_state = WIFI_ST_FAILED;
            ESP_LOGW(TAG, "disconnected (reason %d, %s), retrying", d->reason, s_error);
            schedule_retry_locked();
        }
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_SCAN_DONE) {
        collect_scan_locked();
        s_scanning = false;
        ESP_LOGI(TAG, "scan done: %d networks", s_ap_count);
        if (!s_connected && !s_attempting) connect_locked();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *e = data;
        snprintf(s_ip, sizeof(s_ip), IPSTR, IP2STR(&e->ip_info.ip));
        s_attempting = false;
        s_connected  = true;
        s_fails      = 0;
        s_error      = NULL;
        s_state      = WIFI_ST_CONNECTED;
        ESP_LOGI(TAG, "connected to '%s', IP %s", s_ssid, s_ip);
    }
    unlock();
}

void wifi_start(void)
{
    s_lock = xSemaphoreCreateMutex();

    helix_settings_t cfg;
    settings_get(&cfg);
    snprintf(s_ssid, sizeof(s_ssid), "%s", cfg.ssid);
    snprintf(s_pass, sizeof(s_pass), "%s", cfg.password);
    s_state = s_ssid[0] ? WIFI_ST_CONNECTING : WIFI_ST_NOT_CONFIGURED;

    const esp_timer_create_args_t retry_args = { .callback = retry_cb, .name = "wifi_retry" };
    const esp_timer_create_args_t scan_args  = { .callback = scan_retry_cb, .name = "wifi_scan" };
    ESP_ERROR_CHECK(esp_timer_create(&retry_args, &s_retry_timer));
    ESP_ERROR_CHECK(esp_timer_create(&scan_args, &s_scan_timer));

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &on_wifi_event, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &on_wifi_event, NULL, NULL));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());   // STA_START triggers the first connect

    if (s_ssid[0]) ESP_LOGI(TAG, "connecting to '%s'", s_ssid);
    else           ESP_LOGW(TAG, "no WiFi configured");
}

void wifi_get_status(wifi_status_t *out)
{
    lock();
    out->state = s_state;
    out->error = s_error;
    snprintf(out->ssid, sizeof(out->ssid), "%s", s_ssid);
    snprintf(out->ip, sizeof(out->ip), "%s", s_connected ? s_ip : "");
    const bool connected = s_connected;
    unlock();

    out->rssi = 0;
    wifi_ap_record_t ap;
    if (connected && esp_wifi_sta_get_ap_info(&ap) == ESP_OK) out->rssi = ap.rssi;
}

void wifi_connect_to(const char *ssid, const char *password)
{
    lock();
    snprintf(s_ssid, sizeof(s_ssid), "%s", ssid ? ssid : "");
    snprintf(s_pass, sizeof(s_pass), "%s", password ? password : "");
    s_fails = 0;
    s_error = NULL;
    s_state = s_ssid[0] ? WIFI_ST_CONNECTING : WIFI_ST_NOT_CONFIGURED;
    esp_timer_stop(s_retry_timer);

    if (s_connected || s_attempting) {
        // Drop the current link; the DISCONNECTED handler connects anew.
        s_switch_pending = true;
        esp_wifi_disconnect();
    } else {
        connect_locked();
    }
    unlock();
}

void wifi_scan_start(void)
{
    lock();
    if (!s_scanning) {
        s_scanning   = true;
        s_scan_tries = 0;
        esp_timer_stop(s_retry_timer);
        if (s_attempting) {
            // A pending connect blocks the scan; cancel it (reconnect after).
            s_attempting = false;
            esp_wifi_disconnect();
        }
        try_scan_locked();
    }
    unlock();
}

int wifi_scan_results(wifi_ap_t *out, int max)
{
    lock();
    int n = -1;
    if (!s_scanning) {
        n = s_ap_count < max ? s_ap_count : max;
        memcpy(out, s_aps, (size_t)n * sizeof(*out));
    }
    unlock();
    return n;
}
