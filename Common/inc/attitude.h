/**
 * @file    attitude.h
 * @brief   九轴姿态解算（x-io Fusion AHRS 封装）
 */

#ifndef ATTITUDE_H
#define ATTITUDE_H

#include "imu.h"
#include "magnetometer.h"
#include "type.h"

#include <stdint.h>

/** 每 N 次 IMU 更新融合一次磁力计（yaw 校正）；2 = 50Hz 下约 25Hz 磁力计 */
#define ATTITUDE_MAG_DECIM          2U

/** 欧拉角：roll / pitch / yaw 均为 -180~180（度，整数） */
typedef struct {
    int16_t roll;
    int16_t pitch;
    int16_t yaw;
} attitude_euler_t;

typedef struct {
    bool_t mag_trust;
    bool_t gyro_bias_ready;
} attitude_status_t;

status_t attitude_init(float sample_hz);
bool_t attitude_is_ready(void);
status_t attitude_update_from_imu(const imu_sample_t *imu);
status_t attitude_update_from_sensors(const imu_sample_t *imu, const magnetometer_sample_t *mag);
status_t attitude_update_step(const imu_sample_t *imu, const magnetometer_sample_t *mag);
status_t attitude_get_euler(attitude_euler_t *euler);
status_t attitude_get_status(attitude_status_t *status);

#endif /* ATTITUDE_H */
