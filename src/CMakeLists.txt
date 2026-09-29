# SPDX-License-Identifier: GPL-3.0-or-later
idf_component_register(
    SRCS
        "app_main.c"
        "wifi.c"
        "moonraker_client.c"
        "display.c"
        "ui.c"
        # Helix typeface — LVGL font arrays restored from the parent project
        "fonts/noto_sans_14.c"
        "fonts/noto_sans_bold_20.c"
    INCLUDE_DIRS
        "."
    REQUIRES
        # esp_lcd: ST7789 panel driver lives here (built into IDF).
        esp_lcd
        # esp_websocket_client pulled in as a managed component
        # (see idf_component.yml); json (cJSON) is bundled with IDF.
        esp_websocket_client
        json
        nvs_flash
        esp_wifi
        esp_event
        esp_netif
        esp_timer
        # "driver" is the monolithic alias that resolves on every IDF 5.x
        # (the esp_driver_gpio/spi/i2c split only exists on 5.2+).
        driver
)
