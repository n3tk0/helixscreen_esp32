# HelixScreen ESP32-S3 (skeleton)

A **standalone ESP-IDF firmware** that puts a HelixScreen-styled touchscreen UI
on an **ESP32-S3** and drives a Klipper printer entirely through the **Moonraker
WebSocket JSON-RPC API**.

> This is **not** a port of the desktop/embedded-Linux HelixScreen app in the
> repo root. That app is ~330k lines of POSIX C++ (libhv, wpa_supplicant,
> BlueZ, OpenGL ES, ThorVG, …) and does not run on a microcontroller. This is a
> **fresh, minimal firmware** that reuses two things from the parent project:
> the **Moonraker protocol** and the **Helix visual language**. Think
> "Helix-skinned KlipperScreen-lite for ESP32," not a recompile.

**Target board:** Waveshare ESP32-S3-Touch-LCD-2.8 (ESP32-S3, ST7789 320×240,
capacitive touch, WiFi). Other ESP32-S3 + SPI-LCD boards work after adjusting
pins in `main/display.c`.

## What works in this skeleton

- WiFi station bring-up (`wifi.c`)
- Moonraker WebSocket client: identify, `printer.objects.subscribe`, parses
  pushed `notify_status_update` frames into a thread-safe snapshot
  (`moonraker_client.c`)
- ST7789 + LVGL 9 display bring-up with PSRAM draw buffers (`display.c`)
- A live dashboard: nozzle/bed temps, print state, file, progress bar, and
  **Pause/Resume** + **E-STOP** buttons wired to real RPCs (`ui.c`)

## What's stubbed / TODO (clearly marked in code)

| Area | Where | Note |
|------|-------|------|
| **Touch input** | `display.c` → `TODO(touch)` | Display renders but isn't interactive until you add the I2C touch controller + `lv_indev`. Controller varies by board revision (CST328 / GT911-class). |
| **Display pins** | `display.c` → `BOARD PINS` block | ⚠️ Verify against the Waveshare wiki for your revision before flashing. |
| **WebSocket frame reassembly** | `moonraker_client.c` → `WEBSOCKET_EVENT_DATA` | Dashboard-sized frames fit one buffer; large replies (file lists) need fragment reassembly. |
| **File browser / print start** | — | `server.files.list` + `printer.print.start` not yet surfaced in the UI. |
| **HelixScreen XML layouts** | — | This skeleton hand-builds LVGL in C. Porting `lib/helix-xml/` (expat + runtime parsing) is a separate, RAM-sensitive investigation. |

## Build & flash

Requires **ESP-IDF v5.1+**.

```bash
cd esp32
idf.py set-target esp32s3

# Set WiFi creds + Moonraker host under:  HelixScreen  →  ...
idf.py menuconfig

idf.py build flash monitor
```

On first build the component manager fetches `lvgl/lvgl` and
`espressif/esp_websocket_client` (see `main/idf_component.yml`) — needs network
access once.

## Layout

```
esp32/
├── CMakeLists.txt          top-level ESP-IDF project
├── sdkconfig.defaults      target, PSRAM, LVGL, partition defaults
├── partitions.csv          16MB flash layout
└── main/
    ├── app_main.c          boot order + LVGL main loop
    ├── wifi.c/.h           WiFi station bring-up
    ├── moonraker_client.c/.h   WebSocket JSON-RPC client + snapshot
    ├── display.c/.h        ST7789 + LVGL wiring (PINS HERE)
    ├── ui.c/.h             the dashboard
    ├── Kconfig.projbuild    menuconfig options (WiFi / Moonraker)
    └── idf_component.yml     managed component deps
```

## Architecture note

The threading model mirrors the parent project's hard-won rule: the WebSocket
runs on its own task and **never touches LVGL** — it only writes a
mutex-guarded snapshot. The LVGL task reads that snapshot on a timer. This is
the MCU-scale equivalent of HelixScreen's `UpdateQueue` main-thread/background-
thread separation.

## Roadmap ideas

1. Add touch + a second screen (controls: jog, home, temp presets).
2. File browser via `server.files.list` → `printer.print.start`.
3. Cache thumbnails to the `storage` SPIFFS partition.
4. Evaluate running `lib/helix-xml` layouts directly vs. hand-built LVGL.
