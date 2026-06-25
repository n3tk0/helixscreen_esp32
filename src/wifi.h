// SPDX-License-Identifier: GPL-3.0-or-later
//
// Minimal WiFi-station bring-up. Blocks until connected (or retries forever).
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Connect to the SSID configured via menuconfig (CONFIG_HELIX_WIFI_*).
// Returns once an IP has been acquired. Requires nvs_flash_init() first.
void wifi_connect_blocking(void);

#ifdef __cplusplus
}
#endif
