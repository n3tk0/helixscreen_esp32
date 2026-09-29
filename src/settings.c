// SPDX-License-Identifier: GPL-3.0-or-later
#include "settings.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "nvs.h"
#include "sdkconfig.h"

static const char *TAG = "settings";
static const char *NS  = "helix";

static helix_settings_t  s_cfg;
static SemaphoreHandle_t s_lock;

static void copy_str(char *dst, size_t cap, const char *src)
{
    strncpy(dst, src ? src : "", cap - 1);
    dst[cap - 1] = '\0';
}

// Read one string key; leaves `dst` untouched if the key isn't stored.
static void load_str(nvs_handle_t h, const char *key, char *dst, size_t cap)
{
    size_t len = cap;
    if (nvs_get_str(h, key, dst, &len) != ESP_OK) return;
    dst[cap - 1] = '\0';
}

void settings_load(void)
{
    if (!s_lock) s_lock = xSemaphoreCreateMutex();

    helix_settings_t cfg = { .port = CONFIG_HELIX_MOONRAKER_PORT };
    copy_str(cfg.ssid, sizeof(cfg.ssid), CONFIG_HELIX_WIFI_SSID);
    copy_str(cfg.password, sizeof(cfg.password), CONFIG_HELIX_WIFI_PASSWORD);
    copy_str(cfg.host, sizeof(cfg.host), CONFIG_HELIX_MOONRAKER_HOST);

    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) == ESP_OK) {
        load_str(h, "ssid", cfg.ssid, sizeof(cfg.ssid));
        load_str(h, "pass", cfg.password, sizeof(cfg.password));
        load_str(h, "host", cfg.host, sizeof(cfg.host));
        uint16_t port;
        if (nvs_get_u16(h, "port", &port) == ESP_OK && port != 0) cfg.port = port;
        nvs_close(h);
        ESP_LOGI(TAG, "loaded saved settings");
    } else {
        ESP_LOGI(TAG, "no saved settings, using build defaults");
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_cfg = cfg;
    xSemaphoreGive(s_lock);
}

void settings_get(helix_settings_t *out)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_cfg;
    xSemaphoreGive(s_lock);
}

// Open, run `write`, commit. Returns false (and logs) on any NVS error.
static bool nvs_write(esp_err_t (*write)(nvs_handle_t, const helix_settings_t *),
                      const helix_settings_t *cfg)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = write(h, cfg);
        if (err == ESP_OK) err = nvs_commit(h);
        nvs_close(h);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "save failed: %s", esp_err_to_name(err));
        return false;
    }
    return true;
}

static esp_err_t write_wifi(nvs_handle_t h, const helix_settings_t *cfg)
{
    esp_err_t err = nvs_set_str(h, "ssid", cfg->ssid);
    return err == ESP_OK ? nvs_set_str(h, "pass", cfg->password) : err;
}

static esp_err_t write_moonraker(nvs_handle_t h, const helix_settings_t *cfg)
{
    esp_err_t err = nvs_set_str(h, "host", cfg->host);
    return err == ESP_OK ? nvs_set_u16(h, "port", cfg->port) : err;
}

bool settings_save_wifi(const char *ssid, const char *password)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    copy_str(s_cfg.ssid, sizeof(s_cfg.ssid), ssid);
    copy_str(s_cfg.password, sizeof(s_cfg.password), password);
    helix_settings_t cfg = s_cfg;
    xSemaphoreGive(s_lock);
    return nvs_write(write_wifi, &cfg);
}

bool settings_save_moonraker(const char *host, uint16_t port)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    copy_str(s_cfg.host, sizeof(s_cfg.host), host);
    s_cfg.port = port;
    helix_settings_t cfg = s_cfg;
    xSemaphoreGive(s_lock);
    return nvs_write(write_moonraker, &cfg);
}
