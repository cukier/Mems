#include "st7735.h"

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

// Minimal 5x7 bitmap font: only the characters this project's labels/readout
// need (uppercase A/G/P/R/X/Y/Z, digits, '+' '-' '.' ':' and space). Each
// glyph is 7 rows, top to bottom; each byte's bits 4..0 are columns 0..4
// (bit4 = leftmost column), so a row like "01110" reads directly as 0x0E.
typedef struct {
  char ch;
  uint8_t rows[7];
} font_glyph_t;

static const font_glyph_t s_font[] = {
    {'A', {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},
    {'G', {0x0F, 0x10, 0x10, 0x13, 0x11, 0x11, 0x0F}},
    {'P', {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}},
    {'R', {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11}},
    {'X', {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11}},
    {'Y', {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04}},
    {'Z', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F}},
    {'0', {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}},
    {'1', {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}},
    {'2', {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}},
    {'3', {0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E}},
    {'4', {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}},
    {'5', {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E}},
    {'6', {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}},
    {'7', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}},
    {'8', {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}},
    {'9', {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C}},
    {'+', {0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00}},
    {'-', {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00}},
    {'.', {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C}},
    {':', {0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x00}},
    {' ', {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
};
#define FONT_GLYPH_COUNT (sizeof(s_font) / sizeof(s_font[0]))
#define FONT_W 5
#define FONT_H 7
#define FONT_MAX_SCALE 4

static const uint8_t *font_lookup(char c) {
  for (size_t i = 0; i < FONT_GLYPH_COUNT; i++) {
    if (s_font[i].ch == c) {
      return s_font[i].rows;
    }
  }
  return s_font[FONT_GLYPH_COUNT - 1].rows; // space (last entry)
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
