// SPDX-License-Identifier: GPL-3.0-or-later
//
// Persistent user settings (WiFi + Moonraker), stored in NVS.
//
// Values saved from the on-device settings screens win. Until something is
// saved, the menuconfig values (CONFIG_HELIX_*) are used, so a build can still
// ship with credentials baked in.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HELIX_SSID_MAX   32   // 802.11 limit
#define HELIX_PASS_MAX   64   // WPA2 passphrase limit
#define HELIX_HOST_MAX   63

// Firmware version shown under Settings -> About and sent to Moonraker. CI
// stamps the git tag or commit here; local builds say "dev".
#ifndef HELIX_FW_VERSION
#define HELIX_FW_VERSION "dev"
#endif

typedef struct {
    char     ssid[HELIX_SSID_MAX + 1];
    char     password[HELIX_PASS_MAX + 1];
    char     host[HELIX_HOST_MAX + 1];
    uint16_t port;
} helix_settings_t;

// Load from NVS, falling back to menuconfig. Call once, after nvs_flash_init().
void settings_load(void);

// Copy of the current settings. Safe from any task.
void settings_get(helix_settings_t *out);

// Replace the WiFi credentials / Moonraker endpoint and write them to NVS.
bool settings_save_wifi(const char *ssid, const char *password);
bool settings_save_moonraker(const char *host, uint16_t port);

#ifdef __cplusplus
}
#endif
