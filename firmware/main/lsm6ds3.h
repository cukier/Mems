#pragma once

#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// SA0 pin low = 0x6A, SA0 pin high (or floating with pull-up) = 0x6B.
#define LSM6DS3_I2C_ADDR 0x6B

#define LSM6DS3_WHO_AM_I_VAL_A 0x69 // LSM6DS3
#define LSM6DS3_WHO_AM_I_VAL_B 0x6A // LSM6DS3TR-C

typedef struct {
  float x;
  float y;
  float z;
} lsm6ds3_axes_t;

typedef struct {
  lsm6ds3_axes_t accel_g;  // in g
  lsm6ds3_axes_t gyro_dps; // in degrees/s
} lsm6ds3_data_t;

// Initializes the I2C bus/driver and configures the LSM6DS3
// (accel @104Hz/±2g, gyro @104Hz/2000dps).
esp_err_t lsm6ds3_init(i2c_port_t port, gpio_num_t sda_gpio,
                       gpio_num_t scl_gpio, uint32_t clk_speed_hz);

// Reads the WHO_AM_I register; returns ESP_OK if it matches a known value.
esp_err_t lsm6ds3_check_who_am_i(uint8_t *out_val);

// Reads accelerometer + gyroscope and converts to physical units.
esp_err_t lsm6ds3_read(lsm6ds3_data_t *out);

#ifdef __cplusplus
}
#endif
