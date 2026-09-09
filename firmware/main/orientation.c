#include "orientation.h"

#include <math.h>

#define COMPLEMENTARY_ALPHA 0.98f

void orientation_init(orientation_t *o) {
  o->roll_deg = 0.0f;
  o->pitch_deg = 0.0f;
  o->yaw_deg = 0.0f;
}

void orientation_update(orientation_t *o, const lsm6ds3_data_t *data,
                        float dt_s) {
  // Roll/pitch from gravity direction (accurate at rest, noisy under linear
  // acceleration). Standard right-handed convention.
  float accel_roll_deg =
      atan2f(data->accel_g.y, data->accel_g.z) * 180.0f / (float)M_PI;
  float accel_pitch_deg =
      atan2f(-data->accel_g.x, sqrtf(data->accel_g.y * data->accel_g.y +
                                     data->accel_g.z * data->accel_g.z)) *
      180.0f / (float)M_PI;

  // Gyro-integrated roll/pitch (smooth, but drifts on its own).
  float gyro_roll_deg = o->roll_deg + data->gyro_dps.x * dt_s;
  float gyro_pitch_deg = o->pitch_deg + data->gyro_dps.y * dt_s;

  // Complementary filter: mostly trust the gyro short-term, pull toward the
  // accel's absolute reference to cancel gyro drift.
  o->roll_deg = COMPLEMENTARY_ALPHA * gyro_roll_deg +
                (1.0f - COMPLEMENTARY_ALPHA) * accel_roll_deg;
  o->pitch_deg = COMPLEMENTARY_ALPHA * gyro_pitch_deg +
                 (1.0f - COMPLEMENTARY_ALPHA) * accel_pitch_deg;

  // Yaw has no absolute reference without a magnetometer: pure gyro
  // integration, will drift.
  o->yaw_deg += data->gyro_dps.z * dt_s;
  if (o->yaw_deg > 180.0f) {
    o->yaw_deg -= 360.0f;
  } else if (o->yaw_deg < -180.0f) {
    o->yaw_deg += 360.0f;
  }
}
