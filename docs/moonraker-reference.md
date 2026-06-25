# Moonraker API reference (firmware roadmap)

The full set of Moonraker JSON-RPC methods the **desktop HelixScreen** client
used, extracted from the original codebase before this repo was repurposed for
firmware. It's a roadmap reference for what the ESP32 client *could* call — not
everything here is implemented (or sensible) on an MCU.

All methods are JSON-RPC 2.0 over the single WebSocket
(`ws://<host>:7125/websocket`). See `src/moonraker_client.c` for the request
helper (`rpc_call`).

## Implemented in firmware today

| Method | Used for |
|--------|----------|
| `server.connection.identify` | Handshake on connect |
| `printer.objects.subscribe` | Push status (temps, print state, progress) |
| `printer.gcode.script` | Run any G-code (moves, M104/M140, fans, …) |
| `printer.emergency_stop` | E-STOP button |
| `printer.print.pause` / `.resume` / `.cancel` | Print control |

## Status & introspection (cheap to add)

| Method | Notes |
|--------|-------|
| `printer.objects.list` | Enumerate available objects to subscribe to |
| `printer.objects.query` | One-shot status read (vs. subscribe) |
| `printer.info` | Klippy state/version |
| `server.info` | Moonraker/Klippy connection state |
| `server.helix.status` | HelixScreen Moonraker-plugin status (if installed) |

## Print lifecycle & files (next UI screens)

| Method | Notes |
|--------|-------|
| `printer.print.start` | Start a print by filename |
| `server.files.list` | Browse gcodes (needs WS frame reassembly — see TODO) |
| `server.files.metadata` | Thumbnails, est. time, filament |
| `server.files.get_directory` / `.post_directory` | Directory ops |
| `server.files.delete_file` / `.move` / `.copy` | File management |
| `server.history.list` / `.totals` | Print history |
| `server.job_queue.status` / `.start` / `.pause` | Print queue |

## Machine control (power users)

| Method | Notes |
|--------|-------|
| `printer.firmware_restart` | Restart Klipper firmware |
| `printer.restart` | Restart Klipper host |
| `machine.reboot` / `machine.shutdown` | Host power |
| `machine.services.restart` | Restart a systemd service |
| `machine.device_power.devices` | Smart-plug / GPIO power control |
| `machine.system_info` | Host info |

## Notifications (server → client, no request)

Handled in `handle_message()` by checking the `method` field:

| Notification | Notes |
|--------------|-------|
| `notify_status_update` | **Implemented** — drives the dashboard |
| `notify_klippy_ready` / `notify_klippy_disconnected` | Klippy connection transitions |
| `notify_gcode_response` | Console/error output from Klipper |

## Extras the desktop used (likely out of scope for MCU)

`server.database.*` (per-client KV store), `server.spoolman.*` (filament
spools), `server.webcams.list`, `machine.timelapse.*`,
`server.sensors.list`, `server.helix.phase_tracking.*`.
