// SPDX-License-Identifier: GPL-3.0-or-later
//
// Mirrors Waveshare's PWR_Key demo logic. Per the schematic, the power key
// (GPIO6, low when pressed) briefly connects the battery; GPIO7 high keeps the
// battery switch on after the key is released.
#include "power.h"

#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/gpio.h"

static const char *TAG = "power";

#define PIN_PWR_KEY         6
#define PIN_PWR_LATCH       7
#define POLL_MS             100
#define SHUTDOWN_HOLD_MS    2000

static bool key_pressed(void) { return gpio_get_level(PIN_PWR_KEY) == 0; }

static void power_key_task(void *arg)
{
    (void)arg;
    // The key is still down from switching on; don't count that press.
    while (key_pressed()) vTaskDelay(pdMS_TO_TICKS(POLL_MS));

    int held_ms = 0;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
        held_ms = key_pressed() ? held_ms + POLL_MS : 0;
        if (held_ms >= SHUTDOWN_HOLD_MS) {
            ESP_LOGW(TAG, "power key held, switching off");
            gpio_set_level(PIN_PWR_LATCH, 0);
            // On battery power is gone now. On USB we keep running; stop here
            // so the key doesn't retrigger.
            vTaskDelete(NULL);
        }
    }
}

void power_init(void)
{
    const gpio_config_t key = {
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = 1ULL << PIN_PWR_KEY,
    };
    gpio_config(&key);
    const gpio_config_t latch = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 1ULL << PIN_PWR_LATCH,
    };
    gpio_config(&latch);
    gpio_set_level(PIN_PWR_LATCH, 0);
    vTaskDelay(pdMS_TO_TICKS(100));

    if (!key_pressed()) {
        return;   // USB power: nothing to latch
    }
    gpio_set_level(PIN_PWR_LATCH, 1);
    ESP_LOGI(TAG, "started from battery, power latched");
    xTaskCreate(power_key_task, "power_key", 2048, NULL, 3, NULL);
}
