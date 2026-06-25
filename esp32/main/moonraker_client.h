// SPDX-License-Identifier: GPL-3.0-or-later
//
// Moonraker JSON-RPC-over-WebSocket client for ESP32-S3.
//
// This is the ESP32 counterpart to the desktop HelixScreen MoonrakerClient
// (src/api/). It speaks the SAME protocol — ws://<host>:<port>/websocket with
// JSON-RPC 2.0 frames — but uses esp_websocket_client + cJSON instead of
// libhv + nlohmann/json, and is dramatically narrower in scope.
//
// Threading model (mirrors HelixScreen's background-thread → UpdateQueue split):
//   - esp_websocket_client runs its own task and delivers frames to our event
//     handler ON THAT TASK. We parse there and write into a mutex-guarded
//     snapshot struct. We NEVER touch LVGL from the WebSocket task.
//   - The LVGL/UI task polls moonraker_get_state() under the same lock.
// This is the embedded analogue of "WebSocket bg thread must not call
// lv_subject_set_* directly" from the desktop CLAUDE.md.

#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Live printer snapshot. Extend this as you subscribe to more objects.
typedef struct {
    bool   connected;          // WebSocket up + identified
    float  nozzle_temp;        // extruder current temp (°C)
    float  nozzle_target;      // extruder target temp (°C)
    float  bed_temp;           // heater_bed current temp (°C)
    float  bed_target;         // heater_bed target temp (°C)
    float  progress;           // print progress 0.0 – 1.0
    char   print_state[16];    // "standby" | "printing" | "paused" | ...
    char   filename[96];       // current print filename
} helix_printer_state_t;

// Start WiFi-independent: call AFTER the network is up. Spawns the WebSocket
// client (its own task) and kicks off identify + object subscription.
void moonraker_client_start(void);

// Thread-safe snapshot copy. Safe to call from the LVGL/UI task.
void moonraker_get_state(helix_printer_state_t *out);

// --- Control surface (fire-and-forget JSON-RPC) -------------------------------
// Each maps to one Moonraker method. Run any G-code via send_gcode (covers
// moves, homing, M104/M140 temp sets, fans, etc.).
void moonraker_send_gcode(const char *gcode);
void moonraker_emergency_stop(void);
void moonraker_print_pause(void);
void moonraker_print_resume(void);
void moonraker_print_cancel(void);

#ifdef __cplusplus
}
#endif
