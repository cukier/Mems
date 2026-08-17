#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lsm6ds3.h"
#include "st7735.h"

static const char *TAG = "main";

#define I2C_PORT       I2C_NUM_0
#define I2C_SDA_GPIO   GPIO_NUM_21
#define I2C_SCL_GPIO   GPIO_NUM_20
#define I2C_CLK_HZ     400000

// ST7735 1.44" TFT, SPI. Default dev-board wiring; adjust to match yours.
#define TFT_SPI_HOST  SPI2_HOST
#define TFT_MOSI_GPIO GPIO_NUM_7
#define TFT_SCLK_GPIO GPIO_NUM_6
#define TFT_CS_GPIO   GPIO_NUM_10
#define TFT_DC_GPIO   GPIO_NUM_2
#define TFT_RST_GPIO  GPIO_NUM_3
#define TFT_BL_GPIO   GPIO_NUM_4
#define TFT_CLK_HZ    20000000

// Histogram layout: 6 vertical bars (accel x/y/z, gyro x/y/z) growing up or
// down from a center baseline depending on sign.
#define CHART_TOP      6
#define CHART_BOTTOM   122
#define CHART_BASELINE ((CHART_TOP + CHART_BOTTOM) / 2)
#define CHART_HALF_H   ((CHART_BOTTOM - CHART_TOP) / 2)
#define BAR_W          18
#define BAR_GAP        2
#define BAR_MARGIN     2

// Full-scale deflection: accel bar fills half-height at +-2g, gyro bar fills
// half-height at +-250dps (typical range for hand motion, not the sensor's
// full +-2000dps span).
#define ACCEL_PX_PER_G   (CHART_HALF_H / 2.0f)
#define GYRO_PX_PER_DPS  (CHART_HALF_H / 250.0f)

static void draw_baseline(void) {
    st7735_fill_rect(0, CHART_BASELINE, ST7735_WIDTH, 1, ST7735_GRAY);
}

static void draw_bar(int col, float value, float px_per_unit, uint16_t color) {
    int x = BAR_MARGIN + col * (BAR_W + BAR_GAP);

    st7735_fill_rect(x, CHART_TOP, BAR_W, CHART_BOTTOM - CHART_TOP, ST7735_BLACK);

    int bar_h = (int)(value * px_per_unit);
    if (bar_h > CHART_HALF_H) bar_h = CHART_HALF_H;
    if (bar_h < -CHART_HALF_H) bar_h = -CHART_HALF_H;

    if (bar_h >= 0) {
        st7735_fill_rect(x, CHART_BASELINE - bar_h, BAR_W, bar_h, color);
    } else {
        st7735_fill_rect(x, CHART_BASELINE, BAR_W, -bar_h, color);
    }
}

void app_main(void) {
    ESP_ERROR_CHECK(lsm6ds3_init(I2C_PORT, I2C_SDA_GPIO, I2C_SCL_GPIO, I2C_CLK_HZ));

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
    st7735_fill_screen(ST7735_BLACK);
    draw_baseline();

    lsm6ds3_data_t data;
    while (1) {
        esp_err_t err = lsm6ds3_read(&data);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "accel[g]  x=%+.3f y=%+.3f z=%+.3f | gyro[dps] x=%+7.2f y=%+7.2f z=%+7.2f",
                     data.accel_g.x, data.accel_g.y, data.accel_g.z,
                     data.gyro_dps.x, data.gyro_dps.y, data.gyro_dps.z);

            draw_bar(0, data.accel_g.x, ACCEL_PX_PER_G, ST7735_RED);
            draw_bar(1, data.accel_g.y, ACCEL_PX_PER_G, ST7735_GREEN);
            draw_bar(2, data.accel_g.z, ACCEL_PX_PER_G, ST7735_BLUE);
            draw_bar(3, data.gyro_dps.x, GYRO_PX_PER_DPS, ST7735_YELLOW);
            draw_bar(4, data.gyro_dps.y, GYRO_PX_PER_DPS, ST7735_CYAN);
            draw_bar(5, data.gyro_dps.z, GYRO_PX_PER_DPS, ST7735_MAGENTA);

            draw_baseline();
        } else {
            ESP_LOGE(TAG, "read failed: %s", esp_err_to_name(err));
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}
