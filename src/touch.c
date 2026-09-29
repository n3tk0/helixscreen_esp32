// SPDX-License-Identifier: GPL-3.0-or-later
#include "touch.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "sdkconfig.h"

static const char *TAG = "touch";

// Touch sits on its own I2C bus, separate from the IMU/RTC bus on GPIO10/11
// (the external "I2C" header). Pins per the board schematic.
#define PIN_TP_SDA      1
#define PIN_TP_SCL      3
#define PIN_TP_INT      4
#define PIN_TP_RST      2
#define TP_I2C_ADDR     0x1A
#define TP_I2C_HZ       400000
#define TP_I2C_TIMEOUT  50      // ms

// CST328 registers are 16-bit, sent high byte first.
#define CST328_REG_TOUCH_XY     0xD000  // point data, 5 bytes per point
#define CST328_REG_TOUCH_COUNT  0xD005  // low nibble = touch count; write 0 to ack
#define CST328_REG_NORMAL_MODE  0xD109

// Raw coordinates are in the glass's native portrait frame.
#define TP_RAW_W        240
#define TP_RAW_H        320

static i2c_master_dev_handle_t s_dev;

static esp_err_t reg_read(uint16_t reg, uint8_t *buf, size_t len)
{
    const uint8_t addr[2] = { reg >> 8, reg & 0xFF };
    return i2c_master_transmit_receive(s_dev, addr, sizeof(addr), buf, len,
                                       TP_I2C_TIMEOUT);
}

// Writes at most one data byte, which is all the CST328 commands we use need.
static esp_err_t reg_write(uint16_t reg, const uint8_t *val)
{
    const uint8_t buf[3] = { reg >> 8, reg & 0xFF, val ? *val : 0 };
    return i2c_master_transmit(s_dev, buf, val ? 3 : 2, TP_I2C_TIMEOUT);
}

// Read the first touch point in raw portrait coordinates. Mirrors Waveshare's
// driver: read the count, read the point block, then ack by clearing the count.
static bool cst328_read(uint16_t *x, uint16_t *y)
{
    static const uint8_t zero = 0;
    uint8_t count;
    if (reg_read(CST328_REG_TOUCH_COUNT, &count, 1) != ESP_OK) return false;

    const uint8_t points = count & 0x0F;
    if (points == 0 || points > 5) {
        reg_write(CST328_REG_TOUCH_COUNT, &zero);
        return false;
    }

    uint8_t d[5];
    esp_err_t err = reg_read(CST328_REG_TOUCH_XY, d, sizeof(d));
    reg_write(CST328_REG_TOUCH_COUNT, &zero);
    if (err != ESP_OK) return false;

    // 12-bit coordinates: high 8 bits in d[1]/d[2], low nibbles packed in d[3].
    *x = ((uint16_t)d[1] << 4) | (d[3] >> 4);
    *y = ((uint16_t)d[2] << 4) | (d[3] & 0x0F);
    if (*x >= TP_RAW_W) *x = TP_RAW_W - 1;
    if (*y >= TP_RAW_H) *y = TP_RAW_H - 1;
    return true;
}

// Map raw portrait touch to the landscape UI. Derived from the panel's MADCTL
// settings in display.c (the display-side counterpart of each branch).
static void raw_to_ui(uint16_t tx, uint16_t ty, int32_t *ux, int32_t *uy)
{
#if CONFIG_HELIX_DISPLAY_FLIP
    *ux = ty;
    *uy = (TP_RAW_W - 1) - tx;
#else
    *ux = (TP_RAW_H - 1) - ty;
    *uy = tx;
#endif
}

// LVGL polls this from lv_timer_handler (LVGL task, lock held). On release we
// leave data->point alone; LVGL reuses the last pressed coordinate.
static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    uint16_t tx, ty;
    if (cst328_read(&tx, &ty)) {
        raw_to_ui(tx, ty, &data->point.x, &data->point.y);
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

bool touch_init(lv_display_t *disp)
{
    // Hardware reset: RST is active low.
    const gpio_config_t rst = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 1ULL << PIN_TP_RST,
    };
    gpio_config(&rst);
    const gpio_config_t irq = {
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pin_bit_mask = 1ULL << PIN_TP_INT,
    };
    gpio_config(&irq);
    gpio_set_level(PIN_TP_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(PIN_TP_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(100));   // controller boot time after reset

    const i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_1,
        .sda_io_num = PIN_TP_SDA,
        .scl_io_num = PIN_TP_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t bus;
    if (i2c_new_master_bus(&bus_cfg, &bus) != ESP_OK) {
        ESP_LOGE(TAG, "I2C bus init failed");
        return false;
    }
    const i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = TP_I2C_ADDR,
        .scl_speed_hz = TP_I2C_HZ,
    };
    if (i2c_master_bus_add_device(bus, &dev_cfg, &s_dev) != ESP_OK) {
        ESP_LOGE(TAG, "I2C device add failed");
        return false;
    }

    // Presence check before registering the input device.
    if (i2c_master_probe(bus, TP_I2C_ADDR, TP_I2C_TIMEOUT) != ESP_OK) {
        ESP_LOGE(TAG, "CST328 not found at 0x%02X", TP_I2C_ADDR);
        return false;
    }
    reg_write(CST328_REG_NORMAL_MODE, NULL);

    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touch_read_cb);
    lv_indev_set_display(indev, disp);

    ESP_LOGI(TAG, "CST328 touch ready");
    return true;
}
