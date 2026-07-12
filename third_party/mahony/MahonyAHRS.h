/**
 * @file    MahonyAHRS.h
 * @brief   Mahony IMU/AHRS sensor fusion (Robert Mahony, GPL-3.0)
 * @see     https://x-io.co.uk/open-source-imu-and-ahrs-algorithms/
 */

#ifndef MAHONY_AHRS_H
#define MAHONY_AHRS_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void MahonyAHRS_init(float sample_hz, float kp, float ki);
void MahonyAHRS_set_gains(float kp, float ki);
void MahonyAHRS_reset(void);

/** 纯陀螺积分（无 accel/mag PI 反馈），运动跟手 */
void MahonyAHRS_update_gyro_only(float gx, float gy, float gz);

/** 静止时由重力（+ 可选磁力计）直接对齐四元数，不做 PI 校正 */
bool MahonyAHRS_align_from_accel(float ax, float ay, float az);
bool MahonyAHRS_align_from_accel_mag(float ax, float ay, float az, float mx, float my, float mz);

/** 由 accel 校正 roll/pitch，保留当前 yaw（须先陀螺积分） */
bool MahonyAHRS_align_tilt_preserve_yaw(float ax, float ay, float az);

/** 静止时慢速融合 mag yaw；alpha 0~1，须先陀螺积分 */
bool MahonyAHRS_blend_yaw_from_mag(float ax, float ay, float az, float mx, float my, float mz,
                                   float alpha);

void MahonyAHRSupdate(float gx, float gy, float gz, float ax, float ay, float az, float mx, float my,
                      float mz);
void MahonyAHRSupdateIMU(float gx, float gy, float gz, float ax, float ay, float az);

bool MahonyAHRS_get_euler_deg(float *roll_deg, float *pitch_deg, float *yaw_deg);

#ifdef __cplusplus
}
#endif

#endif /* MAHONY_AHRS_H */
