#ifndef _IMU_H_
#define _IMU_H_

#include "esp_err.h"

esp_err_t imu_init(void);
void imu_deinit(void);
void imu_update(void);
void imu_get_data(float *accel_x, float *accel_y, float *accel_z,
                  float *gyro_x, float *gyro_y, float *gyro_z);
void imu_calibrate(void);
void imu_reset_calibration(void);

#endif // _IMU_H_