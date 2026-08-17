#pragma once

#include <stdint.h>
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// 1.44" ST7735 panels are 128x128; the visible area is offset by 2px in
// both axes on most modules (RGB "greentab" variant).
#define ST7735_WIDTH   128
#define ST7735_HEIGHT  128
#define ST7735_X_OFFSET 2
#define ST7735_Y_OFFSET 3

// RGB565 helper.
#define ST7735_RGB565(r, g, b) \
    (uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | (((b) & 0xF8) >> 3))

#define ST7735_BLACK   ST7735_RGB565(0, 0, 0)
#define ST7735_WHITE   ST7735_RGB565(255, 255, 255)
#define ST7735_RED     ST7735_RGB565(255, 0, 0)
#define ST7735_GREEN   ST7735_RGB565(0, 255, 0)
#define ST7735_BLUE    ST7735_RGB565(0, 0, 255)
#define ST7735_YELLOW  ST7735_RGB565(255, 255, 0)
#define ST7735_CYAN    ST7735_RGB565(0, 255, 255)
#define ST7735_MAGENTA ST7735_RGB565(255, 0, 255)
#define ST7735_GRAY    ST7735_RGB565(64, 64, 64)

typedef struct {
    spi_host_device_t host;
    gpio_num_t mosi_gpio;
    gpio_num_t sclk_gpio;
    gpio_num_t cs_gpio;
    gpio_num_t dc_gpio;
    gpio_num_t rst_gpio;
    gpio_num_t bl_gpio;   // set to GPIO_NUM_NC if unused
    int clock_hz;
} st7735_config_t;

esp_err_t st7735_init(const st7735_config_t *cfg);

// Fills a rectangle with a solid RGB565 color. Coordinates are in the
// panel's visible 128x128 area (offsets are applied internally).
esp_err_t st7735_fill_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);

// Fills the whole screen.
esp_err_t st7735_fill_screen(uint16_t color);

#ifdef __cplusplus
}
#endif
