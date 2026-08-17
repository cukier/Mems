#include "st7735.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "st7735";

static spi_device_handle_t s_spi;
static gpio_num_t s_dc_gpio;

// ST7735 command set (subset used here).
#define ST7735_SWRESET 0x01
#define ST7735_SLPOUT  0x11
#define ST7735_COLMOD  0x3A
#define ST7735_MADCTL  0x36
#define ST7735_DISPON  0x29
#define ST7735_CASET   0x2A
#define ST7735_RASET   0x2B
#define ST7735_RAMWR   0x2C

static esp_err_t send(bool is_data, const uint8_t *buf, size_t len) {
    if (len == 0) {
        return ESP_OK;
    }
    gpio_set_level(s_dc_gpio, is_data ? 1 : 0);
    spi_transaction_t t = {
        .length = len * 8,
        .tx_buffer = buf,
    };
    return spi_device_polling_transmit(s_spi, &t);
}

static esp_err_t send_cmd(uint8_t cmd) {
    return send(false, &cmd, 1);
}

static esp_err_t send_data(const uint8_t *data, size_t len) {
    return send(true, data, len);
}

static esp_err_t set_addr_window(int16_t x, int16_t y, int16_t w, int16_t h) {
    uint16_t x0 = x + ST7735_X_OFFSET;
    uint16_t x1 = x0 + w - 1;
    uint16_t y0 = y + ST7735_Y_OFFSET;
    uint16_t y1 = y0 + h - 1;

    uint8_t col[4] = {(uint8_t)(x0 >> 8), (uint8_t)x0, (uint8_t)(x1 >> 8), (uint8_t)x1};
    uint8_t row[4] = {(uint8_t)(y0 >> 8), (uint8_t)y0, (uint8_t)(y1 >> 8), (uint8_t)y1};

    esp_err_t err;
    if ((err = send_cmd(ST7735_CASET)) != ESP_OK) return err;
    if ((err = send_data(col, sizeof(col))) != ESP_OK) return err;
    if ((err = send_cmd(ST7735_RASET)) != ESP_OK) return err;
    if ((err = send_data(row, sizeof(row))) != ESP_OK) return err;
    return send_cmd(ST7735_RAMWR);
}

esp_err_t st7735_init(const st7735_config_t *cfg) {
    s_dc_gpio = cfg->dc_gpio;

    gpio_config_t dc_cfg = {
        .pin_bit_mask = 1ULL << cfg->dc_gpio,
        .mode = GPIO_MODE_OUTPUT,
    };
    esp_err_t err = gpio_config(&dc_cfg);
    if (err != ESP_OK) {
        return err;
    }

    if (cfg->rst_gpio != GPIO_NUM_NC) {
        gpio_config_t rst_cfg = {
            .pin_bit_mask = 1ULL << cfg->rst_gpio,
            .mode = GPIO_MODE_OUTPUT,
        };
        err = gpio_config(&rst_cfg);
        if (err != ESP_OK) {
            return err;
        }
        gpio_set_level(cfg->rst_gpio, 0);
        vTaskDelay(pdMS_TO_TICKS(20));
        gpio_set_level(cfg->rst_gpio, 1);
        vTaskDelay(pdMS_TO_TICKS(150));
    }

    if (cfg->bl_gpio != GPIO_NUM_NC) {
        gpio_config_t bl_cfg = {
            .pin_bit_mask = 1ULL << cfg->bl_gpio,
            .mode = GPIO_MODE_OUTPUT,
        };
        err = gpio_config(&bl_cfg);
        if (err != ESP_OK) {
            return err;
        }
        gpio_set_level(cfg->bl_gpio, 1);
    }

    spi_bus_config_t bus_cfg = {
        .mosi_io_num = cfg->mosi_gpio,
        .miso_io_num = -1,
        .sclk_io_num = cfg->sclk_gpio,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = ST7735_WIDTH * 8 * 2,
    };
    err = spi_bus_initialize(cfg->host, &bus_cfg, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
        return err;
    }

    spi_device_interface_config_t dev_cfg = {
        .clock_speed_hz = cfg->clock_hz,
        .mode = 0,
        .spics_io_num = cfg->cs_gpio,
        .queue_size = 1,
    };
    err = spi_bus_add_device(cfg->host, &dev_cfg, &s_spi);
    if (err != ESP_OK) {
        return err;
    }

    send_cmd(ST7735_SWRESET);
    vTaskDelay(pdMS_TO_TICKS(150));
    send_cmd(ST7735_SLPOUT);
    vTaskDelay(pdMS_TO_TICKS(150));

    uint8_t colmod = 0x05; // 16 bpp
    send_cmd(ST7735_COLMOD);
    send_data(&colmod, 1);

    uint8_t madctl = 0x00;
    send_cmd(ST7735_MADCTL);
    send_data(&madctl, 1);

    send_cmd(ST7735_DISPON);
    vTaskDelay(pdMS_TO_TICKS(50));

    ESP_LOGI(TAG, "init done");
    return ESP_OK;
}

esp_err_t st7735_fill_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    if (w <= 0 || h <= 0) {
        return ESP_OK;
    }
    esp_err_t err = set_addr_window(x, y, w, h);
    if (err != ESP_OK) {
        return err;
    }

    uint16_t be_color = (uint16_t)((color << 8) | (color >> 8));
    static uint16_t line[ST7735_WIDTH];
    for (int i = 0; i < w; i++) {
        line[i] = be_color;
    }

    gpio_set_level(s_dc_gpio, 1);
    for (int16_t row = 0; row < h; row++) {
        spi_transaction_t t = {
            .length = (size_t)w * 16,
            .tx_buffer = line,
        };
        err = spi_device_polling_transmit(s_spi, &t);
        if (err != ESP_OK) {
            return err;
        }
    }
    return ESP_OK;
}

esp_err_t st7735_fill_screen(uint16_t color) {
    return st7735_fill_rect(0, 0, ST7735_WIDTH, ST7735_HEIGHT, color);
}
