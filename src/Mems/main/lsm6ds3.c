#include "lsm6ds3.h"

#include "driver/i2c.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "lsm6ds3";

// Registers used (LSM6DS3 datasheet).
#define REG_WHO_AM_I 0x0F
#define REG_CTRL1_XL 0x10 // accel: ODR + full-scale
#define REG_CTRL2_G 0x11  // gyro:  ODR + full-scale
#define REG_CTRL3_C 0x12  // BDU, IF_INC, etc.
#define REG_OUTX_L_G                                                           \
  0x22 // gyro output, 6 bytes (X,Y,Z), followed by accel at 0x28
#define REG_OUTX_L_XL 0x28 // accel output, 6 bytes (X,Y,Z)

// Sensitivity for the configured full-scale ranges (see lsm6ds3_init).
#define ACCEL_SENS_G_PER_LSB 0.000061f // ±2g
#define GYRO_SENS_DPS_PER_LSB 0.070f   // 2000 dps

static i2c_port_t s_port = I2C_NUM_0;

static esp_err_t reg_write(uint8_t reg, uint8_t val) {
  uint8_t buf[2] = {reg, val};
  return i2c_master_write_to_device(s_port, LSM6DS3_I2C_ADDR, buf, sizeof(buf),
                                    pdMS_TO_TICKS(100));
}

static esp_err_t reg_read(uint8_t reg, uint8_t *data, size_t len) {
  return i2c_master_write_read_device(s_port, LSM6DS3_I2C_ADDR, &reg, 1, data,
                                      len, pdMS_TO_TICKS(100));
}

esp_err_t lsm6ds3_init(i2c_port_t port, gpio_num_t sda_gpio,
                       gpio_num_t scl_gpio, uint32_t clk_speed_hz) {
  s_port = port;

  i2c_config_t conf = {
      .mode = I2C_MODE_MASTER,
      .sda_io_num = sda_gpio,
      .scl_io_num = scl_gpio,
      .sda_pullup_en = GPIO_PULLUP_ENABLE,
      .scl_pullup_en = GPIO_PULLUP_ENABLE,
      .master.clk_speed = clk_speed_hz,
  };
  esp_err_t err = i2c_param_config(s_port, &conf);
  if (err != ESP_OK) {
    return err;
  }
  err = i2c_driver_install(s_port, conf.mode, 0, 0, 0);
  if (err != ESP_OK) {
    return err;
  }

  uint8_t who_am_i = 0;
  err = lsm6ds3_check_who_am_i(&who_am_i);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "unexpected WHO_AM_I=0x%02X", who_am_i);
    return err;
  }
  ESP_LOGI(TAG, "found LSM6DS3, WHO_AM_I=0x%02X", who_am_i);

  // BDU=1 (block data update, avoids reading torn samples), IF_INC=1
  // (auto-increment address).
  err = reg_write(REG_CTRL3_C, 0x44);
  if (err != ESP_OK) {
    return err;
  }
  // Accel: ODR=104Hz, FS=±2g.
  err = reg_write(REG_CTRL1_XL, 0x40);
  if (err != ESP_OK) {
    return err;
  }
  // Gyro: ODR=104Hz, FS=2000dps.
  err = reg_write(REG_CTRL2_G, 0x4C);
  return err;
}

esp_err_t lsm6ds3_check_who_am_i(uint8_t *out_val) {
  uint8_t val = 0;
  esp_err_t err = reg_read(REG_WHO_AM_I, &val, 1);
  if (out_val) {
    *out_val = val;
  }
  if (err != ESP_OK) {
    return err;
  }
  if (val != LSM6DS3_WHO_AM_I_VAL_A && val != LSM6DS3_WHO_AM_I_VAL_B) {
    return ESP_ERR_NOT_FOUND;
  }
  return ESP_OK;
}

esp_err_t lsm6ds3_read(lsm6ds3_data_t *out) {
  uint8_t raw[12];
  // Gyro (0x22..0x27) and accel (0x28..0x2D) are contiguous, read in one burst.
  esp_err_t err = reg_read(REG_OUTX_L_G, raw, sizeof(raw));
  if (err != ESP_OK) {
    return err;
  }

  int16_t gx = (int16_t)((raw[1] << 8) | raw[0]);
  int16_t gy = (int16_t)((raw[3] << 8) | raw[2]);
  int16_t gz = (int16_t)((raw[5] << 8) | raw[4]);
  int16_t ax = (int16_t)((raw[7] << 8) | raw[6]);
  int16_t ay = (int16_t)((raw[9] << 8) | raw[8]);
  int16_t az = (int16_t)((raw[11] << 8) | raw[10]);

  out->gyro_dps.x = gx * GYRO_SENS_DPS_PER_LSB;
  out->gyro_dps.y = gy * GYRO_SENS_DPS_PER_LSB;
  out->gyro_dps.z = gz * GYRO_SENS_DPS_PER_LSB;
  out->accel_g.x = ax * ACCEL_SENS_G_PER_LSB;
  out->accel_g.y = ay * ACCEL_SENS_G_PER_LSB;
  out->accel_g.z = az * ACCEL_SENS_G_PER_LSB;

  return ESP_OK;
}
