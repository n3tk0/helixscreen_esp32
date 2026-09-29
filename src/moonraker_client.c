// SPDX-License-Identifier: GPL-3.0-or-later
#include "moonraker_client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_websocket_client.h"
#include "cJSON.h"
#include "sdkconfig.h"

static const char *TAG = "moonraker";

static esp_websocket_client_handle_t s_client;
static SemaphoreHandle_t             s_lock;       // guards s_state
static helix_printer_state_t         s_state;
static int                           s_next_id = 1;  // atomic: LVGL + WS tasks both send

// Outbound JSON-RPC goes through a queue drained by a dedicated TX task, so
// callers never block on the socket. That matters for the LVGL button
// callbacks (E-STOP, pause, ...): they run on the main task WHILE it holds the
// LVGL lock, and a stalled send there would freeze the whole UI.
#define TX_QUEUE_LEN        16
#define TX_SEND_TIMEOUT_MS  2000
static QueueHandle_t                 s_tx_queue;   // char* (cJSON-allocated)

// Inbound message reassembly. A text message larger than the client's
// buffer_size arrives as several DATA events of one frame (payload_offset > 0),
// and a peer may also split it into continuation frames (op_code 0x0). Only the
// WebSocket task touches these, so they need no lock.
#define RX_MAX_MSG          (64 * 1024)
static char                         *s_rx_buf;
static size_t                        s_rx_len;
static size_t                        s_rx_cap;
static bool                          s_rx_active;    // inside a text message
static bool                          s_rx_overflow;  // current message is being discarded

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

static void state_lock(void)   { xSemaphoreTake(s_lock, portMAX_DELAY); }
static void state_unlock(void) { xSemaphoreGive(s_lock); }

// The only task that writes to the socket. Sends use a bounded timeout, so a
// dead link costs this task a couple of seconds — never the UI.
static void tx_task(void *arg)
{
    (void)arg;
    char *msg;
    for (;;) {
        if (xQueueReceive(s_tx_queue, &msg, portMAX_DELAY) != pdTRUE) continue;
        if (s_client && esp_websocket_client_is_connected(s_client)) {
            int sent = esp_websocket_client_send_text(s_client, msg, strlen(msg),
                                                      pdMS_TO_TICKS(TX_SEND_TIMEOUT_MS));
            if (sent < 0) {
                ESP_LOGW(TAG, "send failed/timed out: %s", msg);
            }
        } else {
            ESP_LOGW(TAG, "drop (not connected): %s", msg);
        }
        cJSON_free(msg);
    }
}

// Build a JSON-RPC request and queue it for the TX task. Never blocks: if the
// queue is full the request is dropped and logged. `params` (may be NULL) is
// taken over by the request object.
static void rpc_call(const char *method, cJSON *params)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "jsonrpc", "2.0");
    cJSON_AddStringToObject(root, "method", method);
    if (params) {
        cJSON_AddItemToObject(root, "params", params);
    }
    cJSON_AddNumberToObject(root, "id",
                            __atomic_fetch_add(&s_next_id, 1, __ATOMIC_RELAXED));

    char *txt = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!txt) return;

    if (!s_tx_queue || xQueueSend(s_tx_queue, &txt, 0) != pdTRUE) {
        ESP_LOGW(TAG, "TX queue full/unavailable, dropping: %s", txt);
        cJSON_free(txt);
    }
}

// ---------------------------------------------------------------------------
// Subscription + identify
// ---------------------------------------------------------------------------

// Subscribe to the objects that drive the dashboard. Moonraker then pushes
// notify_status_update frames whenever any of these change — no polling.
static void subscribe_objects(void)
{
    // params: { "objects": { "extruder": null, "heater_bed": null,
    //                        "print_stats": null, "display_status": null } }
    cJSON *params  = cJSON_CreateObject();
    cJSON *objects = cJSON_AddObjectToObject(params, "objects");
    cJSON_AddNullToObject(objects, "extruder");
    cJSON_AddNullToObject(objects, "heater_bed");
    cJSON_AddNullToObject(objects, "print_stats");
    cJSON_AddNullToObject(objects, "display_status");
    rpc_call("printer.objects.subscribe", params);
}

static void identify(void)
{
    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "client_name", "HelixScreen-ESP32");
    cJSON_AddStringToObject(params, "version", "0.1.0");
    cJSON_AddStringToObject(params, "type", "display");
    cJSON_AddStringToObject(params, "url", "https://github.com/n3tk0/helixscreen_esp32");
#if defined(CONFIG_HELIX_MOONRAKER_API_KEY)
    if (strlen(CONFIG_HELIX_MOONRAKER_API_KEY) > 0) {
        cJSON_AddStringToObject(params, "api_key", CONFIG_HELIX_MOONRAKER_API_KEY);
    }
#endif
    rpc_call("server.connection.identify", params);
}

// ---------------------------------------------------------------------------
// Inbound status parsing
// ---------------------------------------------------------------------------

// Apply one "status" object (the shape shared by printer.objects.query results
// and notify_status_update params[0]) into the snapshot.
static void apply_status(cJSON *status)
{
    if (!cJSON_IsObject(status)) return;

    state_lock();

    cJSON *extruder = cJSON_GetObjectItem(status, "extruder");
    if (extruder) {
        cJSON *t = cJSON_GetObjectItem(extruder, "temperature");
        cJSON *g = cJSON_GetObjectItem(extruder, "target");
        if (cJSON_IsNumber(t)) s_state.nozzle_temp   = (float)t->valuedouble;
        if (cJSON_IsNumber(g)) s_state.nozzle_target = (float)g->valuedouble;
    }

    cJSON *bed = cJSON_GetObjectItem(status, "heater_bed");
    if (bed) {
        cJSON *t = cJSON_GetObjectItem(bed, "temperature");
        cJSON *g = cJSON_GetObjectItem(bed, "target");
        if (cJSON_IsNumber(t)) s_state.bed_temp   = (float)t->valuedouble;
        if (cJSON_IsNumber(g)) s_state.bed_target = (float)g->valuedouble;
    }

    cJSON *ps = cJSON_GetObjectItem(status, "print_stats");
    if (ps) {
        cJSON *st = cJSON_GetObjectItem(ps, "state");
        cJSON *fn = cJSON_GetObjectItem(ps, "filename");
        if (cJSON_IsString(st)) {
            strncpy(s_state.print_state, st->valuestring, sizeof(s_state.print_state) - 1);
            s_state.print_state[sizeof(s_state.print_state) - 1] = '\0';
        }
        if (cJSON_IsString(fn)) {
            strncpy(s_state.filename, fn->valuestring, sizeof(s_state.filename) - 1);
            s_state.filename[sizeof(s_state.filename) - 1] = '\0';
        }
    }

    cJSON *ds = cJSON_GetObjectItem(status, "display_status");
    if (ds) {
        cJSON *p = cJSON_GetObjectItem(ds, "progress");
        if (cJSON_IsNumber(p)) s_state.progress = (float)p->valuedouble;
    }

    state_unlock();
}

static void handle_message(const char *data, int len)
{
    cJSON *root = cJSON_ParseWithLength(data, len);
    if (!root) {
        ESP_LOGW(TAG, "unparseable frame (%d bytes)", len);
        return;
    }

    cJSON *method = cJSON_GetObjectItem(root, "method");
    if (cJSON_IsString(method)) {
        // Server-initiated notification.
        if (strcmp(method->valuestring, "notify_status_update") == 0) {
            // params == [ {status...}, eventtime ]
            cJSON *params = cJSON_GetObjectItem(root, "params");
            cJSON *status = cJSON_GetArrayItem(params, 0);
            apply_status(status);
        }
        // Other notifications (notify_klippy_ready, notify_gcode_response, ...)
        // can be handled here as the UI grows.
    } else {
        // Reply to one of our requests. The subscribe reply carries the full
        // initial status under result.status — seed the snapshot from it.
        cJSON *result = cJSON_GetObjectItem(root, "result");
        if (result) {
            cJSON *status = cJSON_GetObjectItem(result, "status");
            if (status) apply_status(status);
        }
    }

    cJSON_Delete(root);
}

// Feed one DATA event into the reassembly buffer; dispatch the message once
// its last slice arrives. Every slice of a frame carries that frame's op_code,
// fin and payload_len, with payload_offset advancing.
static void on_ws_data(const esp_websocket_event_data_t *d)
{
    switch (d->op_code) {
    case 0x1:   // text frame (first slice starts a new message)
        if (d->payload_offset == 0) {
            s_rx_len      = 0;
            s_rx_active   = true;
            s_rx_overflow = false;
        }
        break;
    case 0x0:   // continuation frame of a fragmented message
        break;
    default:    // ping/pong/close/binary — not part of a text message
        return;
    }
    if (!s_rx_active) return;   // continuation with no message started

    if (!s_rx_overflow && d->data_len > 0) {
        size_t need = s_rx_len + (size_t)d->data_len;
        if (need > RX_MAX_MSG) {
            ESP_LOGW(TAG, "message exceeds %d bytes, dropping", RX_MAX_MSG);
            s_rx_overflow = true;
        } else {
            if (need > s_rx_cap) {
                size_t cap = s_rx_cap ? s_rx_cap : 4096;
                while (cap < need) cap *= 2;
                if (cap > RX_MAX_MSG) cap = RX_MAX_MSG;
                // Large buffers land in PSRAM via CONFIG_SPIRAM_USE_MALLOC.
                char *grown = realloc(s_rx_buf, cap);
                if (grown) {
                    s_rx_buf = grown;
                    s_rx_cap = cap;
                } else {
                    ESP_LOGE(TAG, "rx buffer alloc failed (%u bytes)", (unsigned)cap);
                    s_rx_overflow = true;
                }
            }
            if (!s_rx_overflow) {
                memcpy(s_rx_buf + s_rx_len, d->data_ptr, d->data_len);
                s_rx_len = need;
            }
        }
    }

    const bool frame_done = d->payload_offset + d->data_len >= d->payload_len;
    if (frame_done && d->fin) {
        if (!s_rx_overflow && s_rx_len > 0) {
            handle_message(s_rx_buf, (int)s_rx_len);
        }
        s_rx_active = false;
    }
}

// ---------------------------------------------------------------------------
// WebSocket lifecycle
// ---------------------------------------------------------------------------

static void ws_event_handler(void *arg, esp_event_base_t base,
                             int32_t event_id, void *event_data)
{
    esp_websocket_event_data_t *d = (esp_websocket_event_data_t *)event_data;

    switch (event_id) {
    case WEBSOCKET_EVENT_CONNECTED:
        ESP_LOGI(TAG, "connected");
        state_lock();
        s_state.connected = true;
        state_unlock();
        identify();
        subscribe_objects();
        break;

    case WEBSOCKET_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "disconnected");
        state_lock();
        s_state.connected = false;
        state_unlock();
        s_rx_active = false;   // a half-received message is gone with the link
        break;

    case WEBSOCKET_EVENT_DATA:
        on_ws_data(d);
        break;

    default:
        break;
    }
}

void moonraker_client_start(void)
{
    if (!s_lock) {
        s_lock = xSemaphoreCreateMutex();
        if (!s_lock) {
            ESP_LOGE(TAG, "failed to create state mutex");
            return;
        }
        memset(&s_state, 0, sizeof(s_state));
        strcpy(s_state.print_state, "offline");
    }

    if (!s_tx_queue) {
        s_tx_queue = xQueueCreate(TX_QUEUE_LEN, sizeof(char *));
        if (!s_tx_queue ||
            xTaskCreate(tx_task, "moonraker_tx", 4096, NULL, 5, NULL) != pdPASS) {
            ESP_LOGE(TAG, "failed to start TX task");
            return;
        }
    }

    char uri[160];
    snprintf(uri, sizeof(uri), "ws://%s:%d/websocket",
             CONFIG_HELIX_MOONRAKER_HOST, CONFIG_HELIX_MOONRAKER_PORT);
    ESP_LOGI(TAG, "connecting to %s", uri);

    esp_websocket_client_config_t cfg = {
        .uri                 = uri,
        .reconnect_timeout_ms = 5000,
        .network_timeout_ms   = 10000,
        // Per-read chunk size; larger messages are reassembled in on_ws_data.
        .buffer_size          = 4096,
    };

    s_client = esp_websocket_client_init(&cfg);
    if (!s_client) {
        ESP_LOGE(TAG, "failed to init WebSocket client");
        return;
    }
    esp_websocket_register_events(s_client, WEBSOCKET_EVENT_ANY,
                                  ws_event_handler, NULL);
    esp_websocket_client_start(s_client);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void moonraker_get_state(helix_printer_state_t *out)
{
    if (!out || !s_lock) return;
    state_lock();
    *out = s_state;
    state_unlock();
}

void moonraker_send_gcode(const char *gcode)
{
    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "script", gcode);
    rpc_call("printer.gcode.script", params);
}

void moonraker_emergency_stop(void) { rpc_call("printer.emergency_stop", NULL); }
void moonraker_print_pause(void)    { rpc_call("printer.print.pause", NULL); }
void moonraker_print_resume(void)   { rpc_call("printer.print.resume", NULL); }
void moonraker_print_cancel(void)   { rpc_call("printer.print.cancel", NULL); }
