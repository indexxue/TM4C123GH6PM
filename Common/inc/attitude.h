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
/** 控制用 float yaw（同 get_euler 平滑路径）与 gz（°/s，车体 Z） */
status_t attitude_get_yaw_control(float *yaw_deg, float *yaw_rate_dps);
status_t attitude_get_status(attitude_status_t *status);

/**
 * 角度环 HOLD：为真时禁止磁力计 yaw 慢融合，航向由 gz 积分（仍保留 gyro 零偏修正）。
 * active=FALSE 后按帧渐变恢复 mag 融合系数。
 */
void attitude_yaw_hold_set(bool_t active);
bool_t attitude_yaw_hold_is_active(void);

/**
 * 静止水平时用磁力计校准物理 yaw：当前朝向设为 ref_yaw_deg（-180~180°），offset 写入 NVS。
 */
status_t attitude_calibrate_yaw(float ref_yaw_deg, float *offset_deg_out, float *yaw_deg_out);
void attitude_set_mag_heading_offset(float offset_deg);

#endif /* ATTITUDE_H */
