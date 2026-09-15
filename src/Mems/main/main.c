#include <stdio.h>

#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "invoke_ble.h"
#include "invoke_game.h"
#include "lsm6ds3.h"
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

// The screen belongs to invoke_game: standby (band number + BLE address) when
// idle, then COUNTDOWN / CAPTURE / ACK per spec §3.3. app_main just feeds it
// IMU samples.
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

  lsm6ds3_data_t data;
  while (1) {
    esp_err_t err = lsm6ds3_read(&data);
    if (err == ESP_OK) {
      invoke_game_tick(&data);
    } else {
      ESP_LOGE(TAG, "read failed: %s", esp_err_to_name(err));
    }
    vTaskDelay(pdMS_TO_TICKS(200));
  }
}
