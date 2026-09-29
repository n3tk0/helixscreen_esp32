# HelixScreen ESP32-S3 (skeleton)

A **standalone ESP-IDF firmware** that puts a HelixScreen-styled touchscreen UI
on an **ESP32-S3** and drives a Klipper printer entirely through the **Moonraker
WebSocket JSON-RPC API**.

> This is **not** a port of the desktop/embedded-Linux HelixScreen app (~330k
> lines of POSIX C++ — libhv, wpa_supplicant, BlueZ, OpenGL ES, ThorVG — which
> does not run on a microcontroller). It's a **fresh, minimal firmware** that
> reuses two things from that project: the **Moonraker protocol** and the
> **Helix visual language**. Think "Helix-skinned KlipperScreen-lite for
> ESP32," not a recompile. (The original desktop codebase lived in this repo's
> history; it was removed when the repo was repurposed for the firmware —
> recover it from git history if needed.)

**Target board:** Waveshare ESP32-S3-Touch-LCD-2.8 (ESP32-S3, ST7789 320×240,
capacitive touch, WiFi). Other ESP32-S3 + SPI-LCD boards work after adjusting
pins in `src/display.c`.

## What works in this skeleton

- WiFi station bring-up (`wifi.c`)
- Moonraker WebSocket client: identify, `printer.objects.subscribe`, parses
  pushed `notify_status_update` frames into a thread-safe snapshot
  (`moonraker_client.c`)
- ST7789 + LVGL 9 display bring-up with PSRAM draw buffers (`display.c`)
- A live dashboard: nozzle/bed temps, print state, file, progress bar, and
  **Pause/Resume** + **E-STOP** buttons wired to real RPCs (`ui.c`)
- **Real Helix look**: the actual Noto Sans typeface (`src/fonts/`) and the
  exact dark-theme design tokens from the parent project's
  `helixscreen.json`, inlined as `COL_*` in `ui.c`
- A Moonraker method roadmap reference: [`docs/moonraker-reference.md`](docs/moonraker-reference.md)

## What's stubbed / TODO (clearly marked in code)

| Area | Where | Note |
|------|-------|------|
| **Touch input** | `display.c` → `TODO(touch)` | Display renders but isn't interactive until you add the I2C touch controller + `lv_indev`. Controller varies by board revision (CST328 / GT911-class). |
| **Display pins** | `display.c` → `BOARD PINS` block | ⚠️ Verify against the Waveshare wiki for your revision before flashing. |
| **WebSocket frame reassembly** | `moonraker_client.c` → `WEBSOCKET_EVENT_DATA` | Dashboard-sized frames fit one buffer; large replies (file lists) need fragment reassembly. |
| **File browser / print start** | — | `server.files.list` + `printer.print.start` not yet surfaced in the UI. |
| **HelixScreen XML layouts** | — | This skeleton hand-builds LVGL in C. Porting the parent project's `helix-xml` engine (expat + runtime parsing) is a separate, RAM-sensitive investigation. |

## Build & flash

Built with **[PlatformIO](https://platformio.org/)** using the **ESP-IDF**
framework. The pinned `platform = espressif32` provides ESP-IDF 5.x.

```bash
# from the repo root
pio run                  # build
pio run -t upload        # flash
pio device monitor       # serial monitor

# Set WiFi creds + Moonraker host (HelixScreen menu):
pio run -t menuconfig
```

On first build PlatformIO installs the ESP-IDF toolchain, and the IDF component
manager fetches `lvgl/lvgl` and `espressif/esp_websocket_client` (see
`src/idf_component.yml`) — needs network access once. CI builds the same way on
GitHub runners via `.github/workflows/esp32-firmware.yml`.

> Plain `idf.py` is not wired up — this project uses PlatformIO's `src/` layout.
> Use `pio run -t menuconfig` for the ESP-IDF config UI.

## Layout

```
.
├── platformio.ini         PlatformIO env (board, framework, partitions)
├── sdkconfig.defaults      target, PSRAM, LVGL, flash/partition defaults
├── partitions.csv          16MB flash layout
├── docs/
│   └── moonraker-reference.md  full Moonraker method roadmap
└── src/
    ├── app_main.c          boot order + LVGL main loop
    ├── wifi.c/.h           WiFi station bring-up
    ├── moonraker_client.c/.h   WebSocket JSON-RPC client + snapshot
    ├── display.c/.h        ST7789 + LVGL wiring (PINS HERE)
    ├── ui.c/.h             the dashboard (Helix tokens inlined)
    ├── fonts/              Noto Sans LVGL font arrays (real Helix typeface)
    ├── CMakeLists.txt       IDF main-component register
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
4. Evaluate running the parent project's `helix-xml` layouts directly vs. hand-built LVGL.
