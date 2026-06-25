// SPDX-License-Identifier: GPL-3.0-or-later
#include "moonraker_client.h"

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_websocket_client.h"
#include "cJSON.h"
#include "sdkconfig.h"

static const char *TAG = "moonraker";

static esp_websocket_client_handle_t s_client;
static SemaphoreHandle_t             s_lock;       // guards s_state
static helix_printer_state_t         s_state;
static int                           s_next_id = 1;

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

static void state_lock(void)   { xSemaphoreTake(s_lock, portMAX_DELAY); }
static void state_unlock(void) { xSemaphoreGive(s_lock); }

// Send a pre-serialized JSON-RPC string over the socket.
static void ws_send(const char *json)
{
    if (!s_client || !esp_websocket_client_is_connected(s_client)) {
        ESP_LOGW(TAG, "drop (not connected): %s", json);
        return;
    }
    esp_websocket_client_send_text(s_client, json, strlen(json), portMAX_DELAY);
}

// Build + send a JSON-RPC request with no params (or caller-supplied params
// object, which we take ownership of). `params` may be NULL.
static void rpc_call(const char *method, cJSON *params)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "jsonrpc", "2.0");
    cJSON_AddStringToObject(root, "method", method);
    if (params) {
        cJSON_AddItemToObject(root, "params", params);
    }
    cJSON_AddNumberToObject(root, "id", s_next_id++);

    char *txt = cJSON_PrintUnformatted(root);
    if (txt) {
        ws_send(txt);
        cJSON_free(txt);
    }
    cJSON_Delete(root);
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
        break;

    case WEBSOCKET_EVENT_DATA:
        // op_code 0x1 == text frame; ignore pings/pongs/continuations here.
        // Note: large frames arrive in fragments — a production client must
        // reassemble using d->payload_offset / d->payload_len. Dashboard-sized
        // status frames fit in one buffer, so the skeleton parses directly.
        if (d->op_code == 0x1 && d->data_len > 0) {
            handle_message((const char *)d->data_ptr, d->data_len);
        }
        break;

    default:
        break;
    }
}

void moonraker_client_start(void)
{
    if (!s_lock) {
        s_lock = xSemaphoreCreateMutex();
        memset(&s_state, 0, sizeof(s_state));
        strcpy(s_state.print_state, "offline");
    }

    char uri[160];
    snprintf(uri, sizeof(uri), "ws://%s:%d/websocket",
             CONFIG_HELIX_MOONRAKER_HOST, CONFIG_HELIX_MOONRAKER_PORT);
    ESP_LOGI(TAG, "connecting to %s", uri);

    esp_websocket_client_config_t cfg = {
        .uri                 = uri,
        .reconnect_timeout_ms = 5000,
        .network_timeout_ms   = 10000,
        // Moonraker status frames can be a few KB; give the RX buffer room.
        .buffer_size          = 4096,
    };

    s_client = esp_websocket_client_init(&cfg);
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
