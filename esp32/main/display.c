// SPDX-License-Identifier: GPL-3.0-or-later
#include "display.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "lvgl.h"

static const char *TAG = "display";

// ============================================================================
// BOARD PINS — Waveshare ESP32-S3-Touch-LCD-2.8
//
// ⚠️  VERIFY these against the schematic/wiki for YOUR board revision before
//     flashing. Pin maps differ across Waveshare LCD variants. The values
//     below are placeholders in the layout the ST7789 SPI boards commonly use.
//     Wiki: https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-2.8
// ============================================================================
#define PIN_LCD_SCLK   40
#define PIN_LCD_MOSI   45
#define PIN_LCD_DC     41
#define PIN_LCD_CS     42
#define PIN_LCD_RST    39
#define PIN_LCD_BL     5     // backlight enable
#define LCD_SPI_HOST   SPI2_HOST
#define LCD_PIXEL_CLOCK_HZ (40 * 1000 * 1000)

static SemaphoreHandle_t      s_lvgl_mutex;
static esp_lcd_panel_handle_t s_panel;

// LVGL 9: flush callback hands a finished region to the panel.
static void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px)
{
    // ST7789 over SPI expects big-endian RGB565; LVGL renders little-endian.
    lv_draw_sw_rgb565_swap(px, lv_area_get_width(area) * lv_area_get_height(area));

    esp_lcd_panel_draw_bitmap(s_panel, area->x1, area->y1,
                              area->x2 + 1, area->y2 + 1, px);
    lv_display_flush_ready(disp);
}

// Drive LVGL's millisecond tick from an esp_timer.
static void lvgl_tick_cb(void *arg)
{
    (void)arg;
    lv_tick_inc(2);
}

bool display_lvgl_lock(int timeout_ms)
{
    TickType_t ticks = (timeout_ms < 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTakeRecursive(s_lvgl_mutex, ticks) == pdTRUE;
}

void display_lvgl_unlock(void)
{
    xSemaphoreGiveRecursive(s_lvgl_mutex);
}

static void panel_init(void)
{
    gpio_config_t bk = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 1ULL << PIN_LCD_BL,
    };
    gpio_config(&bk);

    spi_bus_config_t bus = {
        .sclk_io_num = PIN_LCD_SCLK,
        .mosi_io_num = PIN_LCD_MOSI,
        .miso_io_num = -1,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = HELIX_LCD_H_RES * 80 * sizeof(uint16_t),
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_SPI_HOST, &bus, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_handle_t io = NULL;
    esp_lcd_panel_io_spi_config_t io_cfg = {
        .dc_gpio_num = PIN_LCD_DC,
        .cs_gpio_num = PIN_LCD_CS,
        .pclk_hz = LCD_PIXEL_CLOCK_HZ,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 10,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(
        (esp_lcd_spi_bus_handle_t)LCD_SPI_HOST, &io_cfg, &io));

    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = PIN_LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io, &panel_cfg, &s_panel));

    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));
    // 2.8" is a 240x320 glass driven in landscape (320x240) — swap + mirror.
    // Adjust these three to match your board's orientation.
    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(s_panel, true));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(s_panel, true, false));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(s_panel, true));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel, true));

    gpio_set_level(PIN_LCD_BL, 1);
}

void display_init(void)
{
    s_lvgl_mutex = xSemaphoreCreateRecursiveMutex();

    panel_init();

    lv_init();

    // Two partial draw buffers (1/10 screen each) in PSRAM. Enough for smooth
    // partial-refresh rendering without eating internal SRAM.
    const size_t buf_px = HELIX_LCD_H_RES * HELIX_LCD_V_RES / 10;
    lv_color_t *buf1 = heap_caps_malloc(buf_px * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    lv_color_t *buf2 = heap_caps_malloc(buf_px * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    assert(buf1 && buf2);

    lv_display_t *disp = lv_display_create(HELIX_LCD_H_RES, HELIX_LCD_V_RES);
    lv_display_set_flush_cb(disp, lvgl_flush_cb);
    lv_display_set_buffers(disp, buf1, buf2, buf_px * sizeof(lv_color_t),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);

    const esp_timer_create_args_t tick_args = {
        .callback = &lvgl_tick_cb,
        .name = "lvgl_tick",
    };
    esp_timer_handle_t tick_timer;
    ESP_ERROR_CHECK(esp_timer_create(&tick_args, &tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(tick_timer, 2 * 1000));  // 2ms

    ESP_LOGI(TAG, "display + LVGL ready (%dx%d)", HELIX_LCD_H_RES, HELIX_LCD_V_RES);

    // TODO(touch): init the capacitive touch controller (the 2.8" board uses an
    // I2C controller — CST328/GT911-class depending on revision) and register
    // an lv_indev with a read_cb that reports points. Without this the UI
    // renders but is not interactive. See the Waveshare touch example + the
    // esp_lcd_touch_* components on the registry.
}
