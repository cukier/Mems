#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lsm6ds3.h"

static const char *TAG = "main";

#define I2C_PORT       I2C_NUM_0
#define I2C_SDA_GPIO   GPIO_NUM_21
#define I2C_SCL_GPIO   GPIO_NUM_20
#define I2C_CLK_HZ     400000

void app_main(void) {
    ESP_ERROR_CHECK(lsm6ds3_init(I2C_PORT, I2C_SDA_GPIO, I2C_SCL_GPIO, I2C_CLK_HZ));

    lsm6ds3_data_t data;
    while (1) {
        esp_err_t err = lsm6ds3_read(&data);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "accel[g]  x=%+.3f y=%+.3f z=%+.3f | gyro[dps] x=%+7.2f y=%+7.2f z=%+7.2f",
                     data.accel_g.x, data.accel_g.y, data.accel_g.z,
                     data.gyro_dps.x, data.gyro_dps.y, data.gyro_dps.z);
        } else {
            ESP_LOGE(TAG, "read failed: %s", esp_err_to_name(err));
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}
