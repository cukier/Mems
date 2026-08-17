#pragma once

#include "lsm6ds3.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float roll_deg;   // rotation about X, 0 = level. Absolute (gravity-referenced).
    float pitch_deg;  // rotation about Y, 0 = level. Absolute (gravity-referenced).
    float yaw_deg;     // rotation about Z, 0 = wherever it started. Relative only:
                        // no magnetometer means no absolute heading reference, so
                        // this drifts over time from gyro bias/noise integration.
} orientation_t;

void orientation_init(orientation_t *o);

// Fuses one accel+gyro sample into the running orientation estimate.
// dt_s is the elapsed time since the previous call, in seconds.
void orientation_update(orientation_t *o, const lsm6ds3_data_t *data, float dt_s);

#ifdef __cplusplus
}
#endif
