#pragma once

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// 1.44" ST7735 panels are 128x128; the visible area is offset by 2px in
// both axes on most modules (RGB "greentab" variant).
#define ST7735_WIDTH 128
#define ST7735_HEIGHT 128
#define ST7735_X_OFFSET 2
#define ST7735_Y_OFFSET 3

// RGB565 helper.
#define ST7735_RGB565(r, g, b)                                                 \
  (uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | (((b) & 0xF8) >> 3))

#define ST7735_BLACK ST7735_RGB565(0, 0, 0)
#define ST7735_WHITE ST7735_RGB565(255, 255, 255)
#define ST7735_RED ST7735_RGB565(255, 0, 0)
#define ST7735_GREEN ST7735_RGB565(0, 255, 0)
#define ST7735_BLUE ST7735_RGB565(0, 0, 255)
#define ST7735_YELLOW ST7735_RGB565(255, 255, 0)
#define ST7735_CYAN ST7735_RGB565(0, 255, 255)
#define ST7735_MAGENTA ST7735_RGB565(255, 0, 255)
#define ST7735_GRAY ST7735_RGB565(64, 64, 64)

typedef struct {
  spi_host_device_t host;
  gpio_num_t mosi_gpio;
  gpio_num_t sclk_gpio;
  gpio_num_t cs_gpio;
  gpio_num_t dc_gpio;
  gpio_num_t rst_gpio;
  gpio_num_t bl_gpio; // set to GPIO_NUM_NC if unused
  int clock_hz;
} st7735_config_t;

esp_err_t st7735_init(const st7735_config_t *cfg);

// Fills a rectangle with a solid RGB565 color. Coordinates are in the
// panel's visible 128x128 area (offsets are applied internally).
esp_err_t st7735_fill_rect(int16_t x, int16_t y, int16_t w, int16_t h,
                           uint16_t color);

// Fills the whole screen.
esp_err_t st7735_fill_screen(uint16_t color);

// Draws one character from a fixed 5x7 bitmap font (a small ASCII subset,
// see st7735.c). Unsupported characters render as blank. `scale` multiplies
// each font pixel into a scale x scale block (1 = 5x7 px, 2 = 10x14 px, ...).
esp_err_t st7735_draw_char(int16_t x, int16_t y, char c, uint16_t fg,
                           uint16_t bg, uint8_t scale);

// Draws a left-to-right string using st7735_draw_char, advancing by
// (5 * scale + scale) px per character (5px glyph + 1px gap, scaled).
esp_err_t st7735_draw_text(int16_t x, int16_t y, const char *s, uint16_t fg,
                           uint16_t bg, uint8_t scale);

// Pixel width st7735_draw_text() would occupy for `s` at `scale`.
int16_t st7735_text_width(const char *s, uint8_t scale);

// Greedy word-wrap: draws `s` across as many lines as needed to keep each
// within `max_w` px, stepping `line_h` px per line (also breaks on '\n').
// Returns the y just past the last line drawn.
int16_t st7735_draw_text_wrapped(int16_t x, int16_t y, int16_t max_w,
                                 int16_t line_h, const char *s, uint16_t fg,
                                 uint16_t bg, uint8_t scale);

// Transliterates UTF-8 text to the printable-ASCII the 5x7 font covers
// (Portuguese accents -> plain letters, e.g. "ção" -> "cao"); unknown
// multibyte sequences become '?'. Always NUL-terminates within `dstsz`.
void st7735_ascii_fold(char *dst, size_t dstsz, const char *src);

#ifdef __cplusplus
}
#endif
