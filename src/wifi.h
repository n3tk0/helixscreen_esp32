// SPDX-License-Identifier: GPL-3.0-or-later
//
// Minimal WiFi-station bring-up. Non-blocking: connects (and reconnects) in the
// background, so the UI can show "connecting..." meanwhile.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Start connecting to the SSID configured via menuconfig (CONFIG_HELIX_WIFI_*)
// and return immediately. Requires nvs_flash_init() first.
void wifi_start(void);

#ifdef __cplusplus
}
#endif
