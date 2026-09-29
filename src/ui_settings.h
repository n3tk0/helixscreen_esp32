// SPDX-License-Identifier: GPL-3.0-or-later
//
// On-device settings: WiFi (scan, password, verify) and the printer address.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// The settings menu (from the dashboard's gear button).
void ui_settings_open(void);

// Straight to the WiFi network list, e.g. on first boot with nothing saved.
void ui_wifi_setup_open(void);

#ifdef __cplusplus
}
#endif
