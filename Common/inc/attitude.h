/**
 * @file    attitude.h
 * @brief   九轴姿态解算（Madgwick AHRS 封装）
 */

#ifndef ATTITUDE_H
#define ATTITUDE_H

#include "imu.h"
#include "magnetometer.h"
#include "type.h"

#include <stdint.h>

/** 欧拉角，单位 0.1°（例：123 表示 12.3°） */
typedef struct {
    int16_t roll_x10;
    int16_t pitch_x10;
    int16_t yaw_x10;
} attitude_euler_t;

status_t attitude_init(float sample_hz);
bool_t attitude_is_ready(void);
status_t attitude_update_from_sensors(const imu_sample_t *imu, const magnetometer_sample_t *mag);
status_t attitude_get_euler(attitude_euler_t *euler);

#endif /* ATTITUDE_H */
