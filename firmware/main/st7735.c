#include "st7735.h"

#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "st7735";

static spi_device_handle_t s_spi;
static gpio_num_t s_dc_gpio;

// ST7735 command set (subset used here).
#define ST7735_SWRESET 0x01
#define ST7735_SLPOUT 0x11
#define ST7735_COLMOD 0x3A
#define ST7735_MADCTL 0x36
#define ST7735_DISPON 0x29
#define ST7735_CASET 0x2A
#define ST7735_RASET 0x2B
#define ST7735_RAMWR 0x2C

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

static esp_err_t send_cmd(uint8_t cmd) { return send(false, &cmd, 1); }

static esp_err_t send_data(const uint8_t *data, size_t len) {
  return send(true, data, len);
}

static esp_err_t set_addr_window(int16_t x, int16_t y, int16_t w, int16_t h) {
  uint16_t x0 = x + ST7735_X_OFFSET;
  uint16_t x1 = x0 + w - 1;
  uint16_t y0 = y + ST7735_Y_OFFSET;
  uint16_t y1 = y0 + h - 1;

  uint8_t col[4] = {(uint8_t)(x0 >> 8), (uint8_t)x0, (uint8_t)(x1 >> 8),
                    (uint8_t)x1};
  uint8_t row[4] = {(uint8_t)(y0 >> 8), (uint8_t)y0, (uint8_t)(y1 >> 8),
                    (uint8_t)y1};

  esp_err_t err;
  if ((err = send_cmd(ST7735_CASET)) != ESP_OK)
    return err;
  if ((err = send_data(col, sizeof(col))) != ESP_OK)
    return err;
  if ((err = send_cmd(ST7735_RASET)) != ESP_OK)
    return err;
  if ((err = send_data(row, sizeof(row))) != ESP_OK)
    return err;
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

esp_err_t st7735_fill_rect(int16_t x, int16_t y, int16_t w, int16_t h,
                           uint16_t color) {
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

// Full printable-ASCII 5x7 bitmap font, 0x20..0x7E. Each glyph is 7 rows
// (top to bottom); a byte's bits 4..0 are columns 0..4 (bit4 = leftmost), so
// a row like "01110" reads directly as 0x0E. Text past ASCII should be passed
// through st7735_ascii_fold() first (accents -> plain letters).
#define FONT_W 5
#define FONT_H 7
#define FONT_MAX_SCALE 4
#define FONT_FIRST 0x20
#define FONT_LAST 0x7E

static const uint8_t s_font5x7[FONT_LAST - FONT_FIRST + 1][7] = {
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 0x20 space
    {0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04}, // !
    {0x0A, 0x0A, 0x00, 0x00, 0x00, 0x00, 0x00}, // "
    {0x0A, 0x0A, 0x1F, 0x0A, 0x1F, 0x0A, 0x0A}, // #
    {0x04, 0x0F, 0x14, 0x0E, 0x05, 0x1E, 0x04}, // $
    {0x18, 0x19, 0x02, 0x04, 0x08, 0x13, 0x03}, // %
    {0x0C, 0x12, 0x14, 0x08, 0x15, 0x12, 0x0D}, // &
    {0x04, 0x04, 0x08, 0x00, 0x00, 0x00, 0x00}, // '
    {0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02}, // (
    {0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08}, // )
    {0x00, 0x04, 0x15, 0x0E, 0x15, 0x04, 0x00}, // *
    {0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00}, // +
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x08}, // ,
    {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00}, // -
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C}, // .
    {0x01, 0x02, 0x04, 0x04, 0x04, 0x08, 0x10}, // /
    {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}, // 0
    {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}, // 1
    {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}, // 2
    {0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E}, // 3
    {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}, // 4
    {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E}, // 5
    {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}, // 6
    {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}, // 7
    {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}, // 8
    {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C}, // 9
    {0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x00}, // :
    {0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x04, 0x08}, // ;
    {0x02, 0x04, 0x08, 0x10, 0x08, 0x04, 0x02}, // <
    {0x00, 0x00, 0x1F, 0x00, 0x1F, 0x00, 0x00}, // =
    {0x08, 0x04, 0x02, 0x01, 0x02, 0x04, 0x08}, // >
    {0x0E, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04}, // ?
    {0x0E, 0x11, 0x17, 0x15, 0x17, 0x10, 0x0E}, // @
    {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}, // A
    {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E}, // B
    {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E}, // C
    {0x1C, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1C}, // D
    {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}, // E
    {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10}, // F
    {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F}, // G
    {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}, // H
    {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E}, // I
    {0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C}, // J
    {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}, // K
    {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F}, // L
    {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11}, // M
    {0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11}, // N
    {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}, // O
    {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}, // P
    {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D}, // Q
    {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11}, // R
    {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}, // S
    {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}, // T
    {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}, // U
    {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04}, // V
    {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A}, // W
    {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11}, // X
    {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04}, // Y
    {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F}, // Z
    {0x0E, 0x08, 0x08, 0x08, 0x08, 0x08, 0x0E}, // [
    {0x10, 0x08, 0x04, 0x04, 0x04, 0x02, 0x01}, // backslash
    {0x0E, 0x02, 0x02, 0x02, 0x02, 0x02, 0x0E}, // ]
    {0x04, 0x0A, 0x11, 0x00, 0x00, 0x00, 0x00}, // ^
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F}, // _
    {0x08, 0x04, 0x02, 0x00, 0x00, 0x00, 0x00}, // `
    {0x00, 0x00, 0x0E, 0x01, 0x0F, 0x11, 0x0F}, // a
    {0x10, 0x10, 0x16, 0x19, 0x11, 0x11, 0x1E}, // b
    {0x00, 0x00, 0x0E, 0x10, 0x10, 0x11, 0x0E}, // c
    {0x01, 0x01, 0x0D, 0x13, 0x11, 0x11, 0x0F}, // d
    {0x00, 0x00, 0x0E, 0x11, 0x1F, 0x10, 0x0E}, // e
    {0x06, 0x09, 0x08, 0x1C, 0x08, 0x08, 0x08}, // f
    {0x00, 0x0F, 0x11, 0x11, 0x0F, 0x01, 0x0E}, // g
    {0x10, 0x10, 0x16, 0x19, 0x11, 0x11, 0x11}, // h
    {0x04, 0x00, 0x0C, 0x04, 0x04, 0x04, 0x0E}, // i
    {0x02, 0x00, 0x06, 0x02, 0x02, 0x12, 0x0C}, // j
    {0x10, 0x10, 0x12, 0x14, 0x18, 0x14, 0x12}, // k
    {0x0C, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E}, // l
    {0x00, 0x00, 0x1A, 0x15, 0x15, 0x15, 0x15}, // m
    {0x00, 0x00, 0x16, 0x19, 0x11, 0x11, 0x11}, // n
    {0x00, 0x00, 0x0E, 0x11, 0x11, 0x11, 0x0E}, // o
    {0x00, 0x00, 0x1E, 0x11, 0x1E, 0x10, 0x10}, // p
    {0x00, 0x00, 0x0D, 0x13, 0x0F, 0x01, 0x01}, // q
    {0x00, 0x00, 0x16, 0x19, 0x10, 0x10, 0x10}, // r
    {0x00, 0x00, 0x0F, 0x10, 0x0E, 0x01, 0x1E}, // s
    {0x08, 0x08, 0x1C, 0x08, 0x08, 0x09, 0x06}, // t
    {0x00, 0x00, 0x11, 0x11, 0x11, 0x13, 0x0D}, // u
    {0x00, 0x00, 0x11, 0x11, 0x11, 0x0A, 0x04}, // v
    {0x00, 0x00, 0x11, 0x11, 0x15, 0x15, 0x0A}, // w
    {0x00, 0x00, 0x11, 0x0A, 0x04, 0x0A, 0x11}, // x
    {0x00, 0x00, 0x11, 0x11, 0x0F, 0x01, 0x0E}, // y
    {0x00, 0x00, 0x1F, 0x02, 0x04, 0x08, 0x1F}, // z
    {0x02, 0x04, 0x04, 0x08, 0x04, 0x04, 0x02}, // {
    {0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}, // |
    {0x08, 0x04, 0x04, 0x02, 0x04, 0x04, 0x08}, // }
    {0x00, 0x00, 0x08, 0x15, 0x02, 0x00, 0x00}, // ~
};

static const uint8_t *font_lookup(char c) {
  unsigned char u = (unsigned char)c;
  if (u < FONT_FIRST || u > FONT_LAST) {
    return s_font5x7[0]; // space
  }
  return s_font5x7[u - FONT_FIRST];
}

esp_err_t st7735_draw_char(int16_t x, int16_t y, char c, uint16_t fg,
                           uint16_t bg, uint8_t scale) {
  if (scale == 0) {
    scale = 1;
  }
  if (scale > FONT_MAX_SCALE) {
    scale = FONT_MAX_SCALE;
  }
  const uint8_t *rows = font_lookup(c);
  int16_t cell_w = FONT_W * scale;
  int16_t cell_h = FONT_H * scale;

  esp_err_t err = set_addr_window(x, y, cell_w, cell_h);
  if (err != ESP_OK) {
    return err;
  }

  uint16_t fg_be = (uint16_t)((fg << 8) | (fg >> 8));
  uint16_t bg_be = (uint16_t)((bg << 8) | (bg >> 8));
  static uint16_t line[FONT_W * FONT_MAX_SCALE];

  gpio_set_level(s_dc_gpio, 1);
  for (int r = 0; r < FONT_H; r++) {
    uint8_t rowbits = rows[r];
    int idx = 0;
    for (int col = 0; col < FONT_W; col++) {
      int bit = (rowbits >> (FONT_W - 1 - col)) & 1;
      uint16_t px = bit ? fg_be : bg_be;
      for (int s = 0; s < scale; s++) {
        line[idx++] = px;
      }
    }
    spi_transaction_t t = {
        .length = (size_t)cell_w * 16,
        .tx_buffer = line,
    };
    for (int s = 0; s < scale; s++) {
      err = spi_device_polling_transmit(s_spi, &t);
      if (err != ESP_OK) {
        return err;
      }
    }
  }
  return ESP_OK;
}

esp_err_t st7735_draw_text(int16_t x, int16_t y, const char *s, uint16_t fg,
                           uint16_t bg, uint8_t scale) {
  int16_t advance = (int16_t)((FONT_W + 1) * (scale == 0 ? 1 : scale));
  int16_t cx = x;
  for (const char *p = s; *p != '\0'; p++) {
    esp_err_t err = st7735_draw_char(cx, y, *p, fg, bg, scale);
    if (err != ESP_OK) {
      return err;
    }
    cx = (int16_t)(cx + advance);
  }
  return ESP_OK;
}

int16_t st7735_text_width(const char *s, uint8_t scale) {
  if (scale == 0) {
    scale = 1;
  }
  size_t n = strlen(s);
  if (n == 0) {
    return 0;
  }
  return (int16_t)((n * (FONT_W + 1) - 1) * scale);
}

int16_t st7735_draw_text_wrapped(int16_t x, int16_t y, int16_t max_w,
                                 int16_t line_h, const char *s, uint16_t fg,
                                 uint16_t bg, uint8_t scale) {
  if (scale == 0) {
    scale = 1;
  }
  int16_t char_w = (int16_t)((FONT_W + 1) * scale);
  int max_chars = max_w / (char_w > 0 ? char_w : 1);
  if (max_chars < 1) {
    max_chars = 1;
  }
  char line[48];
  if (max_chars > (int)sizeof(line) - 1) {
    max_chars = (int)sizeof(line) - 1;
  }

  int16_t cy = y;
  const char *p = s;
  while (*p) {
    while (*p == ' ') {
      p++;
    }
    if (!*p) {
      break;
    }
    int len = 0, last_space = -1;
    while (p[len] && p[len] != '\n' && len < max_chars) {
      if (p[len] == ' ') {
        last_space = len;
      }
      len++;
    }
    int take = len;
    if (p[len] && p[len] != '\n' && p[len] != ' ' && last_space > 0) {
      take = last_space; // break at the last word boundary that fit
    }
    memcpy(line, p, (size_t)take);
    line[take] = '\0';
    st7735_draw_text(x, cy, line, fg, bg, scale);
    cy = (int16_t)(cy + line_h);
    p += take;
    if (*p == '\n') {
      p++;
    }
  }
  return cy;
}

void st7735_ascii_fold(char *dst, size_t dstsz, const char *src) {
  if (dstsz == 0) {
    return;
  }
  size_t o = 0;
  for (const unsigned char *p = (const unsigned char *)src;
       *p && o + 1 < dstsz;) {
    unsigned char c = *p++;
    if (c < 0x80) {
      dst[o++] = (char)c;
      continue;
    }
    if (c == 0xC3 && *p) {
      unsigned char n = *p++;
      char r;
      switch (n) {
      case 0xA0: case 0xA1: case 0xA2: case 0xA3: case 0xA4: case 0xA5: r = 'a'; break;
      case 0x80: case 0x81: case 0x82: case 0x83: case 0x84: case 0x85: r = 'A'; break;
      case 0xA7: r = 'c'; break;
      case 0x87: r = 'C'; break;
      case 0xA8: case 0xA9: case 0xAA: case 0xAB: r = 'e'; break;
      case 0x88: case 0x89: case 0x8A: case 0x8B: r = 'E'; break;
      case 0xAC: case 0xAD: case 0xAE: case 0xAF: r = 'i'; break;
      case 0x8C: case 0x8D: case 0x8E: case 0x8F: r = 'I'; break;
      case 0xB2: case 0xB3: case 0xB4: case 0xB5: case 0xB6: r = 'o'; break;
      case 0x92: case 0x93: case 0x94: case 0x95: case 0x96: r = 'O'; break;
      case 0xB9: case 0xBA: case 0xBB: case 0xBC: r = 'u'; break;
      case 0x99: case 0x9A: case 0x9B: case 0x9C: r = 'U'; break;
      case 0xB1: r = 'n'; break;
      case 0x91: r = 'N'; break;
      default: r = '?'; break;
      }
      dst[o++] = r;
      continue;
    }
    // any other multibyte lead: emit one '?', skip its continuation bytes
    if (c >= 0xC0) {
      dst[o++] = '?';
    }
    while ((*p & 0xC0) == 0x80) {
      p++;
    }
  }
  dst[o] = '\0';
}
