// SPDX-License-Identifier: GPL-3.0-or-later
//
// Battery power latch for the Waveshare ESP32-S3-Touch-LCD-2.8.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Call first thing in app_main. When the board was switched on from battery
// with the power key, this latches power on (otherwise it cuts out when the
// key is released) and a later 2-second hold of the key switches it off.
// On USB power it does nothing.
void power_init(void);

#ifdef __cplusplus
}
#endif
