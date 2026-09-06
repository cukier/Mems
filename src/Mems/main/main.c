#include <stdio.h>

#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "invoke_ble.h"
#include "invoke_game.h"
#include "lsm6ds3.h"
#include "orientation.h"
#include "st7735.h"

static const char* TAG = "main";

#define I2C_PORT I2C_NUM_0
#define I2C_SDA_GPIO GPIO_NUM_21
#define I2C_SCL_GPIO GPIO_NUM_20
#define I2C_CLK_HZ 400000

// ST7735 1.44" TFT, SPI. Wiring per board schematic
// (docs/7036188100_1733311334_org.png): LCD_RST=IO5 LCD_DC=IO0 LCD_MOSI=IO4
// LCD_SCK=IO3 LCD_CS=IO2. Backlight (LEDA) is hard-wired to 3V3 through R11,
// not GPIO-controlled.
#define TFT_SPI_HOST SPI2_HOST
#define TFT_MOSI_GPIO GPIO_NUM_4
#define TFT_SCLK_GPIO GPIO_NUM_3
#define TFT_CS_GPIO GPIO_NUM_2
#define TFT_DC_GPIO GPIO_NUM_0
#define TFT_RST_GPIO GPIO_NUM_5
#define TFT_BL_GPIO GPIO_NUM_NC
#define TFT_CLK_HZ 20000000

// Histogram layout: 6 vertical bars (accel x/y/z, gyro x/y/z) growing up or
// down from a center baseline depending on sign. A label row sits above the
// chart, and the roll/pitch/yaw readout sits below it.
#define LABEL_Y 0
#define VERSION_Y 9
#define CHART_TOP 18
#define CHART_BOTTOM 98
#define CHART_BASELINE ((CHART_TOP + CHART_BOTTOM) / 2)
#define CHART_HALF_H ((CHART_BOTTOM - CHART_TOP) / 2)
#define BAR_W 18
#define BAR_GAP 2
#define BAR_MARGIN 2
#define ORIENT_Y0 102
#define ORIENT_LINE_H 9

// Full-scale deflection: accel bar fills half-height at +-2g, gyro bar fills
// half-height at +-250dps (typical range for hand motion, not the sensor's
// full +-2000dps span).
#define ACCEL_PX_PER_G (CHART_HALF_H / 2.0f)
#define GYRO_PX_PER_DPS (CHART_HALF_H / 250.0f)

static const char* const BAR_LABELS[6] = {"AX", "AY", "AZ", "GX", "GY", "GZ"};
static const uint16_t BAR_COLORS[6] = {
    ST7735_RED,    ST7735_GREEN, ST7735_BLUE,
    ST7735_YELLOW, ST7735_CYAN,  ST7735_MAGENTA,
};

static void draw_bar_labels(void) {
  for (int i = 0; i < 6; i++) {
    int x = BAR_MARGIN + i * (BAR_W + BAR_GAP) + 3;
    st7735_draw_text(x, LABEL_Y, BAR_LABELS[i], BAR_COLORS[i], ST7735_BLACK, 1);
  }
}

// Shows which build is actually flashed — invaluable when debugging "did my
// update actually take" over BLE, where there's no other way to tell from
// the outside. Deliberately *not* esp_app_get_description()->version/time:
// that's a git-describe string plus a compile timestamp baked in by a
// separate ESP-IDF-generated translation unit, and neither reliably
// refreshes under an incremental build unless the commit changes (git
// describe) or that specific file happens to get recompiled (its
// __TIME__/__DATE__) — exactly the "did this rebuild actually happen"
// signal this exists to answer can go stale. __TIME__ expanded right here
// in main.c is guaranteed fresh on every build that touches this file,
// which in practice is every build during active development.
static void draw_fw_version(void) {
  st7735_draw_text(BAR_MARGIN, VERSION_Y, "built " __TIME__, ST7735_WHITE,
                   ST7735_BLACK, 1);
}

static void draw_baseline(void) {
  st7735_fill_rect(0, CHART_BASELINE, ST7735_WIDTH, 1, ST7735_GRAY);
}

// The one-time-per-"session" dashboard chrome: drawn at boot, and again
// whenever invoke_game hands the screen back after a question — its
// COUNTDOWN/CAPTURE/ACK screens overwrite the whole panel, so the dashboard
// needs a full repaint rather than just resuming its per-sample bar/text
// updates, which only ever touch their own small regions.
static void draw_dashboard_chrome(void) {
  st7735_fill_screen(ST7735_BLACK);
  draw_bar_labels();
  draw_fw_version();
  draw_baseline();
}

static void draw_bar(int col, float value, float px_per_unit, uint16_t color) {
  int x = BAR_MARGIN + col * (BAR_W + BAR_GAP);

  st7735_fill_rect(x, CHART_TOP, BAR_W, CHART_BOTTOM - CHART_TOP, ST7735_BLACK);

  int bar_h = (int)(value * px_per_unit);
  if (bar_h > CHART_HALF_H)
    bar_h = CHART_HALF_H;
  if (bar_h < -CHART_HALF_H)
    bar_h = -CHART_HALF_H;

  if (bar_h >= 0) {
    st7735_fill_rect(x, CHART_BASELINE - bar_h, BAR_W, bar_h, color);
  } else {
    st7735_fill_rect(x, CHART_BASELINE, BAR_W, -bar_h, color);
  }
}

static void draw_orientation(const orientation_t* o) {
  char line[16];
  snprintf(line, sizeof(line), "R:%+04d", (int)o->roll_deg);
  st7735_draw_text(BAR_MARGIN, ORIENT_Y0, line, ST7735_WHITE, ST7735_BLACK, 1);
  snprintf(line, sizeof(line), "P:%+04d", (int)o->pitch_deg);
  st7735_draw_text(BAR_MARGIN, ORIENT_Y0 + ORIENT_LINE_H, line, ST7735_WHITE,
                   ST7735_BLACK, 1);
  snprintf(line, sizeof(line), "Y:%+04d", (int)o->yaw_deg);
  st7735_draw_text(BAR_MARGIN, ORIENT_Y0 + 2 * ORIENT_LINE_H, line,
                   ST7735_WHITE, ST7735_BLACK, 1);
}

void app_main(void) {
  ESP_ERROR_CHECK(
      lsm6ds3_init(I2C_PORT, I2C_SDA_GPIO, I2C_SCL_GPIO, I2C_CLK_HZ));
  ESP_ERROR_CHECK(invoke_ble_init());

  st7735_config_t tft_cfg = {
      .host = TFT_SPI_HOST,
      .mosi_gpio = TFT_MOSI_GPIO,
      .sclk_gpio = TFT_SCLK_GPIO,
      .cs_gpio = TFT_CS_GPIO,
      .dc_gpio = TFT_DC_GPIO,
      .rst_gpio = TFT_RST_GPIO,
      .bl_gpio = TFT_BL_GPIO,
      .clock_hz = TFT_CLK_HZ,
  };
  ESP_ERROR_CHECK(st7735_init(&tft_cfg));
  invoke_game_init();
  draw_dashboard_chrome();

  orientation_t orientation;
  orientation_init(&orientation);
  int64_t last_us = esp_timer_get_time();
  bool was_idle = true;

  lsm6ds3_data_t data;
  while (1) {
    esp_err_t err = lsm6ds3_read(&data);
    if (err == ESP_OK) {
      int64_t now_us = esp_timer_get_time();
      float dt_s = (now_us - last_us) / 1e6f;
      last_us = now_us;
      orientation_update(&orientation, &data, dt_s);

      // ESP_LOGI(TAG,
      //          "accel[g]  x=%+.3f y=%+.3f z=%+.3f | gyro[dps] x=%+7.2f "
      //          "y=%+7.2f z=%+7.2f | "
      //          "roll=%+6.1f pitch=%+6.1f yaw=%+6.1f (yaw drifts, no compass)",
      //          data.accel_g.x, data.accel_g.y, data.accel_g.z, data.gyro_dps.x,
      //          data.gyro_dps.y, data.gyro_dps.z, orientation.roll_deg,
      //          orientation.pitch_deg, orientation.yaw_deg);

      invoke_game_tick(&data);
      bool idle = invoke_game_is_idle();
      if (idle) {
        if (!was_idle) {
          // A question just finished (COUNTDOWN/CAPTURE/ACK owned the
          // screen); repaint the dashboard chrome those screens overwrote.
          draw_dashboard_chrome();
        }
        draw_bar(0, data.accel_g.x, ACCEL_PX_PER_G, ST7735_RED);
        draw_bar(1, data.accel_g.y, ACCEL_PX_PER_G, ST7735_GREEN);
        draw_bar(2, data.accel_g.z, ACCEL_PX_PER_G, ST7735_BLUE);
        draw_bar(3, data.gyro_dps.x, GYRO_PX_PER_DPS, ST7735_YELLOW);
        draw_bar(4, data.gyro_dps.y, GYRO_PX_PER_DPS, ST7735_CYAN);
        draw_bar(5, data.gyro_dps.z, GYRO_PX_PER_DPS, ST7735_MAGENTA);

        draw_baseline();
        draw_orientation(&orientation);
      }
      was_idle = idle;
    } else {
      ESP_LOGE(TAG, "read failed: %s", esp_err_to_name(err));
    }
    vTaskDelay(pdMS_TO_TICKS(200));
  }
}
