# SPDX-License-Identifier: GPL-3.0-or-later
idf_component_register(
    SRCS
        "app_main.c"
        "wifi.c"
        "moonraker_client.c"
        "display.c"
        "ui.c"
    INCLUDE_DIRS
        "."
    REQUIRES
        # esp_lcd: ST7789 panel driver lives here (built into IDF).
        esp_lcd
        # esp_websocket_client + cJSON pulled in as managed components
        # (see idf_component.yml) but listed here so headers resolve.
        esp_websocket_client
        json
        nvs_flash
        esp_wifi
        esp_driver_gpio
        esp_driver_spi
        esp_driver_i2c
)
