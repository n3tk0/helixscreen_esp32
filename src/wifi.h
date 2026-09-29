// SPDX-License-Identifier: GPL-3.0-or-later
//
// WiFi station: background connect/reconnect, network scan, and trying new
// credentials before they're saved. All calls are non-blocking and safe from
// the LVGL task.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    WIFI_ST_NOT_CONFIGURED,   // no SSID set
    WIFI_ST_CONNECTING,
    WIFI_ST_CONNECTED,
    WIFI_ST_FAILED,           // several attempts failed; still retrying
} wifi_state_t;

typedef struct {
    wifi_state_t state;
    char         ssid[33];
    char         ip[16];      // valid when CONNECTED
    int8_t       rssi;        // valid when CONNECTED
    const char  *error;       // why the last attempt failed, or NULL
} wifi_status_t;

typedef struct {
    char   ssid[33];
    int8_t rssi;
    bool   secure;
} wifi_ap_t;

// Bring up the station and connect to the saved network, if any.
// Requires nvs_flash_init() and settings_load() first.
void wifi_start(void);

void wifi_get_status(wifi_status_t *out);

// Switch to these credentials (not saved). Watch wifi_get_status() for
// CONNECTED or FAILED. Pass the saved credentials to go back to them.
void wifi_connect_to(const char *ssid, const char *password);

// Start a network scan. Results via wifi_scan_results().
void wifi_scan_start(void);

// Copy up to `max` networks, strongest first, one entry per SSID.
// Returns -1 while a scan is still running.
int wifi_scan_results(wifi_ap_t *out, int max);

#ifdef __cplusplus
}
#endif
