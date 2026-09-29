// SPDX-License-Identifier: GPL-3.0-or-later
//
// HelixScreen ESP32-S3 — entry point.
//
// Boot order:
//   1. Power latch (must be first: on battery the board cuts out otherwise)
//   2. NVS + saved settings
//   3. WiFi station — returns at once, connects in the background (the UI
//      reads its status and scans, so it must exist before the UI)
//   4. Display + LVGL + touch, then the dashboard (or WiFi setup if unset)
//   5. Moonraker WebSocket client (its own task; retries until WiFi is up)
//   6. Pump LVGL forever on this (main) task, under the LVGL lock
//
// The Moonraker client task writes a mutex-guarded snapshot; the LVGL refresh
// timer reads it. No LVGL call ever happens off this task. That separation is
// the MCU-scale version of HelixScreen's main-thread / WebSocket-thread split.

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "lvgl.h"

#include "power.h"
#include "settings.h"
#include "wifi.h"
#include "display.h"
#include "ui.h"
#include "moonraker_client.h"

static const char *TAG = "helix";

void app_main(void)
{
    power_init();
    ESP_LOGI(TAG, "HelixScreen ESP32-S3 starting");

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    settings_load();
    wifi_start();

    if (!display_init()) {
        // No LVGL display exists, so ui_create() would dereference a NULL
        // screen. Stop here; the error is already logged.
        ESP_LOGE(TAG, "display init failed, not starting UI");
        return;
    }

    if (display_lvgl_lock(-1)) {
        ui_create();
        display_lvgl_unlock();
    }

    moonraker_client_start();

    // LVGL main loop. lv_timer_handler() must be serialized against any other
    // LVGL access, so hold the lock around it.
    while (true) {
        uint32_t delay_ms = 5;
        if (display_lvgl_lock(-1)) {
            delay_ms = lv_timer_handler();
            display_lvgl_unlock();
        }
        if (delay_ms > 100) delay_ms = 100;   // cap so input stays responsive
        if (delay_ms < 5)   delay_ms = 5;
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}
