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

**Target board:** Waveshare ESP32-S3-Touch-LCD-2.8 (ESP32-S3R8 with 8 MB
octal PSRAM, 16 MB flash, ST7789T 240×320 panel run in landscape, CST328
capacitive touch). Other ESP32-S3 + SPI-LCD boards work after adjusting the
pins in `src/display.c` and `src/touch.c`.

### Board pins

Checked against the board schematic and Waveshare's ESP-IDF demo.

| Function | GPIO |
|----------|------|
| LCD SCK / MOSI / CS / D/C / RST | 40 / 45 / 42 / 41 / 39 |
| LCD backlight (high = on) | 5 |
| Touch SDA / SCL / INT / RST (CST328, I2C 0x1A) | 1 / 3 / 4 / 2 |
| Power key / battery power latch | 6 / 7 |
| I2C header, IMU (QMI8658) + RTC (PCF85063) — unused | SDA 11 / SCL 10 |

If the UI shows upside down for how you mount the board, enable
**HelixScreen → Rotate the display 180 degrees** in menuconfig; touch follows.
On battery, hold the power key to switch on, and hold it for 2 s to switch off.

## Setting it up (on the device)

No computer needed after flashing:

1. On first boot, with no WiFi saved, the screen opens the **WiFi networks**
   list and scans. Tap your network (or **Other network...** for a hidden one).
2. Type the password. Text is entered from **one scrolling row** holding every
   letter, digit and symbol: swipe the row, or tap **abc / ABC / 123 / #+=** to
   jump to that part of it, then tap characters. The eye button shows/hides the
   password; tap in the field to move the cursor.
3. **Connect** tries the password first and only saves it once the connection
   works. A wrong password or missing network is reported with **Try again**,
   and cancelling goes back to the previously saved network.
4. Open **Settings** (gear, top right of the dashboard) → **Printer** and enter
   your Moonraker address, e.g. `192.168.1.20` or `mainsailos.local:7125`.

Settings are stored in flash (NVS) and survive updates. The menuconfig values
(`pio run -t menuconfig` → HelixScreen) are only defaults until something is
saved on the device.

## What works in this skeleton

- WiFi in the background with retry/backoff, network scan, and
  test-before-save credential changes (`wifi.c`)
- On-device settings: WiFi scanner, single-row text entry, printer address
  (`ui_settings.c`, `ui_textinput.c`), saved to NVS (`settings.c`)
- Moonraker WebSocket client: identify, `printer.objects.subscribe`, parses
  pushed `notify_status_update` frames into a thread-safe snapshot
  (`moonraker_client.c`)
- ST7789T + LVGL 9 display bring-up with Waveshare's panel init sequence and
  PSRAM draw buffers (`display.c`)
- CST328 touch as an LVGL pointer input (`touch.c`)
- Battery power latch and long-press power-off (`power.c`)
- A live dashboard: nozzle/bed temps, print state, file, progress bar, and
  **Pause/Resume** + **E-STOP** buttons wired to real RPCs (`ui.c`)
- **Real Helix look**: the actual Noto Sans typeface (`src/fonts/`) and the
  exact dark-theme design tokens from the parent project's
  `helixscreen.json`, inlined as `COL_*` in `ui.c`
- A Moonraker method roadmap reference: [`docs/moonraker-reference.md`](docs/moonraker-reference.md)

## What's stubbed / TODO (clearly marked in code)

| Area | Where | Note |
|------|-------|------|
| **Untested on hardware** | `display.c`, `touch.c` | Builds cleanly, but panel orientation/colours and the touch coordinate mapping haven't been checked on a real board yet. |
| **File browser / print start** | — | `server.files.list` + `printer.print.start` not yet surfaced in the UI. |
| **HelixScreen XML layouts** | — | This skeleton hand-builds LVGL in C. Porting the parent project's `helix-xml` engine (expat + runtime parsing) is a separate, RAM-sensitive investigation. |

## Getting the firmware

### Ready-made (GitHub Actions)

`.github/workflows/build-firmware.yml` builds every push and pull request, and
can be started by hand under **Actions → Build Firmware → Run workflow**. Each
run's summary page has an artifact with:

| File | Flash at | Use |
|------|----------|-----|
| `helixscreen-esp32s3-<version>-full.bin` | `0x0` | first install, or to reset a board |
| `helixscreen-esp32s3-<version>-app.bin` | `0x10000` | update a board already running it |
| `SHA256SUMS` | | checksums |

Publishing a **GitHub Release** builds it with the release tag as the version
and attaches these files to the release.

Flash the full image with the [ESP web flasher](https://espressif.github.io/esptool-js/)
(Chrome/Edge, USB) at address `0x0`, or:

```bash
esptool.py --chip esp32s3 write_flash 0x0 helixscreen-esp32s3-<version>-full.bin
```

If the board doesn't show up, hold **BOOT**, tap **RESET**, release **BOOT**.

### Building yourself

Built with **[PlatformIO](https://platformio.org/)** using the **ESP-IDF**
framework. The pinned `platform = espressif32` provides ESP-IDF 5.x.

```bash
# from the repo root
pio run                  # build
pio run -t upload        # flash
pio device monitor       # serial monitor
pio run -t menuconfig    # optional build-time defaults (HelixScreen menu)

# the same release files CI makes, into output/
python3 tools/package_firmware.py --env waveshare_s3_touch_28 --name helixscreen-esp32s3-dev
```

On first build PlatformIO installs the ESP-IDF toolchain, and the IDF component
manager fetches `lvgl/lvgl` and `espressif/esp_websocket_client` (see
`src/idf_component.yml`) — needs network access once.

> Plain `idf.py` is not wired up — this project uses PlatformIO's `src/` layout.
> Use `pio run -t menuconfig` for the ESP-IDF config UI.

## Layout

```
.
├── platformio.ini         PlatformIO env (board, framework, partitions)
├── sdkconfig.defaults      target, PSRAM, LVGL, flash/partition defaults
├── partitions.csv          16MB flash layout
├── tools/package_firmware.py   full/app images + size check (used by CI)
├── docs/
│   └── moonraker-reference.md  full Moonraker method roadmap
└── src/
    ├── app_main.c          boot order + LVGL main loop
    ├── wifi.c/.h           WiFi station bring-up
    ├── moonraker_client.c/.h   WebSocket JSON-RPC client + snapshot
    ├── display.c/.h        ST7789 + LVGL wiring (PINS HERE)
    ├── ui_common.c/.h      theme colours, fonts, screen/header/button helpers
    ├── ui_settings.c/.h    settings menu, WiFi scanner, connect, printer address
    ├── ui_textinput.c/.h   single-row character entry
    ├── settings.c/.h       NVS-backed settings (menuconfig as defaults)
    ├── touch.c/.h          CST328 touch → LVGL pointer input
    ├── power.c/.h          battery power latch + power key
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

1. A controls screen: jog, home, temperature presets.
2. File browser via `server.files.list` → `printer.print.start`.
3. Cache thumbnails to the `storage` SPIFFS partition.
4. Evaluate running the parent project's `helix-xml` layouts directly vs. hand-built LVGL.
