/**
 * @file    attitude.c
 * @brief   Madgwick AHRS 九轴姿态解算封装
 */

#include "attitude.h"

#include "MadgwickAHRS.h"

#include <math.h>

/** MPU6050 上电默认：±2g */
#define ATTITUDE_ACCEL_LSB_PER_G 16384.0f
/** MPU6050 上电默认：±250 °/s */
#define ATTITUDE_GYRO_LSB_PER_DPS 131.0f
#define ATTITUDE_DEG2RAD 0.01745329252f
#define ATTITUDE_BETA_GAIN 0.1f

static bool_t s_ready;

static int16_t attitude_deg_to_x10(float deg)
{
    if (deg >= 0.0f) {
        return (int16_t)(deg * 10.0f + 0.5f);
    }
    return (int16_t)(deg * 10.0f - 0.5f);
}

status_t attitude_init(float sample_hz)
{
    if (sample_hz <= 0.0f) {
        return STATUS_INVALID_ARG;
    }

    MadgwickAHRS_init(sample_hz, ATTITUDE_BETA_GAIN);
    s_ready = TRUE;
    return STATUS_OK;
}

bool_t attitude_is_ready(void)
{
    return s_ready;
}

status_t attitude_update_from_sensors(const imu_sample_t *imu, const magnetometer_sample_t *mag)
{
    float gx;
    float gy;
    float gz;
    float ax;
    float ay;
    float az;
    float mx;
    float my;
    float mz;

    if (s_ready == FALSE) {
        return STATUS_INVALID_STATE;
    }
    if ((imu == NULL) || (mag == NULL)) {
        return STATUS_INVALID_ARG;
    }

    ax = (float)imu->ax / ATTITUDE_ACCEL_LSB_PER_G;
    ay = (float)imu->ay / ATTITUDE_ACCEL_LSB_PER_G;
    az = (float)imu->az / ATTITUDE_ACCEL_LSB_PER_G;

    gx = ((float)imu->gx / ATTITUDE_GYRO_LSB_PER_DPS) * ATTITUDE_DEG2RAD;
    gy = ((float)imu->gy / ATTITUDE_GYRO_LSB_PER_DPS) * ATTITUDE_DEG2RAD;
    gz = ((float)imu->gz / ATTITUDE_GYRO_LSB_PER_DPS) * ATTITUDE_DEG2RAD;

    mx = (float)mag->mx;
    my = (float)mag->my;
    mz = (float)mag->mz;

    MadgwickAHRSupdate(gx, gy, gz, ax, ay, az, mx, my, mz);
    return STATUS_OK;
}

status_t attitude_get_euler(attitude_euler_t *euler)
{
    float roll_deg = 0.0f;
    float pitch_deg = 0.0f;
    float yaw_deg = 0.0f;

    if (s_ready == FALSE) {
        return STATUS_INVALID_STATE;
    }
    if (euler == NULL) {
        return STATUS_INVALID_ARG;
    }

    if (!MadgwickAHRS_get_euler_deg(&roll_deg, &pitch_deg, &yaw_deg)) {
        return STATUS_FAIL;
    }

    euler->roll_x10 = attitude_deg_to_x10(roll_deg);
    euler->pitch_x10 = attitude_deg_to_x10(pitch_deg);
    euler->yaw_x10 = attitude_deg_to_x10(yaw_deg);
    return STATUS_OK;
}
