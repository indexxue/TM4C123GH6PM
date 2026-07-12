/**
 * @file    MahonyAHRS.c
 * @brief   Mahony IMU/AHRS sensor fusion (Robert Mahony, GPL-3.0)
 * @see     https://x-io.co.uk/open-source-imu-and-ahrs-algorithms/
 */

#include "MahonyAHRS.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

static float s_two_kp = 2.0f * 1.0f;
static float s_two_ki = 2.0f * 0.0f;
static float s_inv_sample_freq = 1.0f / 50.0f;
static float s_q0 = 1.0f;
static float s_q1 = 0.0f;
static float s_q2 = 0.0f;
static float s_q3 = 0.0f;
static float s_integral_fb_x = 0.0f;
static float s_integral_fb_y = 0.0f;
static float s_integral_fb_z = 0.0f;
static float s_yaw_ref_rad = 0.0f;
static bool s_yaw_ref_valid = false;

static float mahony_inv_sqrt(float x);
static bool mahony_tilt_from_accel(float ax, float ay, float az, float *roll, float *pitch);

static float mahony_wrap_pi(float rad)
{
    while (rad > 3.14159265f) {
        rad -= 6.28318531f;
    }
    while (rad < -3.14159265f) {
        rad += 6.28318531f;
    }
    return rad;
}

static float mahony_yaw_rad_from_quat(void)
{
    return atan2f(2.0f * (s_q0 * s_q3 + s_q1 * s_q2), 1.0f - 2.0f * (s_q2 * s_q2 + s_q3 * s_q3));
}

/** 相对上一帧最短路径的连续 yaw（弧度，可超出 ±π） */
static float mahony_yaw_rad_continuous(void)
{
    float yaw = mahony_yaw_rad_from_quat();

    if (s_yaw_ref_valid == false) {
        s_yaw_ref_rad = yaw;
        s_yaw_ref_valid = true;
        return s_yaw_ref_rad;
    }

    s_yaw_ref_rad += mahony_wrap_pi(yaw - s_yaw_ref_rad);
    return s_yaw_ref_rad;
}

static void mahony_yaw_ref_set(float yaw_rad)
{
    s_yaw_ref_rad = yaw_rad;
    s_yaw_ref_valid = true;
}

static bool mahony_tilt_from_accel(float ax, float ay, float az, float *roll, float *pitch)
{
    float norm = sqrtf(ax * ax + ay * ay + az * az);

    if (norm < 0.5f) {
        return false;
    }

    ax /= norm;
    ay /= norm;
    az /= norm;
    *roll = atan2f(ay, az);
    *pitch = atan2f(-ax, sqrtf(ay * ay + az * az));
    return true;
}

static bool mahony_yaw_from_accel_mag(float ax, float ay, float az, float mx, float my, float mz,
                                      float *yaw_rad)
{
    float roll;
    float pitch;
    float cos_roll;
    float sin_roll;
    float cos_pitch;
    float sin_pitch;
    float mxh;
    float myh;
    float mag_norm;

    if ((yaw_rad == NULL) || !mahony_tilt_from_accel(ax, ay, az, &roll, &pitch)) {
        return false;
    }

    mag_norm = sqrtf(mx * mx + my * my + mz * mz);
    if (mag_norm < 1.0f) {
        return false;
    }

    cos_roll = cosf(roll);
    sin_roll = sinf(roll);
    cos_pitch = cosf(pitch);
    sin_pitch = sinf(pitch);
    mxh = mx * cos_pitch + my * sin_roll * sin_pitch + mz * cos_roll * sin_pitch;
    myh = my * cos_roll - mz * sin_roll;
    *yaw_rad = atan2f(-myh, mxh);
    return true;
}

static void mahony_quat_from_euler_rad(float roll, float pitch, float yaw)
{
    float cr = cosf(roll * 0.5f);
    float sr = sinf(roll * 0.5f);
    float cp = cosf(pitch * 0.5f);
    float sp = sinf(pitch * 0.5f);
    float cy = cosf(yaw * 0.5f);
    float sy = sinf(yaw * 0.5f);

    s_q0 = cr * cp * cy + sr * sp * sy;
    s_q1 = sr * cp * cy - cr * sp * sy;
    s_q2 = cr * sp * cy + sr * cp * sy;
    s_q3 = cr * cp * sy - sr * sp * cy;
}

void MahonyAHRS_update_gyro_only(float gx, float gy, float gz)
{
    float recip_norm;
    float qa;
    float qb;
    float qc;

    gx *= 0.5f * s_inv_sample_freq;
    gy *= 0.5f * s_inv_sample_freq;
    gz *= 0.5f * s_inv_sample_freq;
    qa = s_q0;
    qb = s_q1;
    qc = s_q2;
    s_q0 += (-qb * gx - qc * gy - s_q3 * gz);
    s_q1 += (qa * gx + qc * gz - s_q3 * gy);
    s_q2 += (qa * gy - qb * gz + s_q3 * gx);
    s_q3 += (qa * gz + qb * gy - qc * gx);

    recip_norm = mahony_inv_sqrt(s_q0 * s_q0 + s_q1 * s_q1 + s_q2 * s_q2 + s_q3 * s_q3);
    s_q0 *= recip_norm;
    s_q1 *= recip_norm;
    s_q2 *= recip_norm;
    s_q3 *= recip_norm;
}

bool MahonyAHRS_align_from_accel(float ax, float ay, float az)
{
    float roll;
    float pitch;
    float yaw;

    if (!mahony_tilt_from_accel(ax, ay, az, &roll, &pitch)) {
        return false;
    }

    yaw = mahony_yaw_rad_continuous();
    mahony_quat_from_euler_rad(roll, pitch, yaw);
    s_integral_fb_x = 0.0f;
    s_integral_fb_y = 0.0f;
    s_integral_fb_z = 0.0f;
    return true;
}

bool MahonyAHRS_align_from_accel_mag(float ax, float ay, float az, float mx, float my, float mz)
{
    float roll;
    float pitch;
    float yaw;

    if (!mahony_tilt_from_accel(ax, ay, az, &roll, &pitch)) {
        return false;
    }

    if (!mahony_yaw_from_accel_mag(ax, ay, az, mx, my, mz, &yaw)) {
        return MahonyAHRS_align_from_accel(ax, ay, az);
    }

    mahony_quat_from_euler_rad(roll, pitch, yaw);
    mahony_yaw_ref_set(yaw);
    s_integral_fb_x = 0.0f;
    s_integral_fb_y = 0.0f;
    s_integral_fb_z = 0.0f;
    return true;
}

bool MahonyAHRS_align_tilt_preserve_yaw(float ax, float ay, float az)
{
    return MahonyAHRS_align_from_accel(ax, ay, az);
}

bool MahonyAHRS_blend_yaw_from_mag(float ax, float ay, float az, float mx, float my, float mz,
                                   float alpha)
{
    float yaw_mag;
    float roll_deg;
    float pitch_deg;
    float yaw_deg;
    float yaw_cur;
    float yaw_new;

    if ((alpha <= 0.0f) || !mahony_yaw_from_accel_mag(ax, ay, az, mx, my, mz, &yaw_mag)) {
        return false;
    }

    if (!MahonyAHRS_get_euler_deg(&roll_deg, &pitch_deg, &yaw_deg)) {
        return false;
    }

    yaw_cur = mahony_yaw_rad_continuous();
    yaw_new = yaw_cur + alpha * mahony_wrap_pi(yaw_mag - yaw_cur);
    mahony_yaw_ref_set(yaw_new);
    mahony_quat_from_euler_rad(roll_deg * 0.01745329252f, pitch_deg * 0.01745329252f, yaw_new);
    return true;
}

void MahonyAHRS_init(float sample_hz, float kp, float ki)
{
    if (sample_hz > 0.0f) {
        s_inv_sample_freq = 1.0f / sample_hz;
    }
    s_two_kp = 2.0f * kp;
    s_two_ki = 2.0f * ki;
    MahonyAHRS_reset();
}

void MahonyAHRS_set_gains(float kp, float ki)
{
    s_two_kp = 2.0f * kp;
    s_two_ki = 2.0f * ki;
}

void MahonyAHRS_reset(void)
{
    s_q0 = 1.0f;
    s_q1 = 0.0f;
    s_q2 = 0.0f;
    s_q3 = 0.0f;
    s_integral_fb_x = 0.0f;
    s_integral_fb_y = 0.0f;
    s_integral_fb_z = 0.0f;
    s_yaw_ref_rad = 0.0f;
    s_yaw_ref_valid = false;
}

bool MahonyAHRS_get_euler_deg(float *roll_deg, float *pitch_deg, float *yaw_deg)
{
    if ((roll_deg == NULL) || (pitch_deg == NULL) || (yaw_deg == NULL)) {
        return false;
    }

    *roll_deg = atan2f(2.0f * (s_q0 * s_q1 + s_q2 * s_q3),
                       1.0f - 2.0f * (s_q1 * s_q1 + s_q2 * s_q2)) * 57.29578f;
    *pitch_deg = asinf(2.0f * (s_q0 * s_q2 - s_q3 * s_q1)) * 57.29578f;
    *yaw_deg = atan2f(2.0f * (s_q0 * s_q3 + s_q1 * s_q2),
                      1.0f - 2.0f * (s_q2 * s_q2 + s_q3 * s_q3)) * 57.29578f;
    return true;
}

void MahonyAHRSupdate(float gx, float gy, float gz, float ax, float ay, float az, float mx, float my,
                      float mz)
{
    float recip_norm;
    float q0q0;
    float q0q1;
    float q0q2;
    float q0q3;
    float q1q1;
    float q1q2;
    float q1q3;
    float q2q2;
    float q2q3;
    float q3q3;
    float hx;
    float hy;
    float bx;
    float bz;
    float half_vx;
    float half_vy;
    float half_vz;
    float half_wx;
    float half_wy;
    float half_wz;
    float half_ex;
    float half_ey;
    float half_ez;
    float qa;
    float qb;
    float qc;

    if ((mx == 0.0f) && (my == 0.0f) && (mz == 0.0f)) {
        MahonyAHRSupdateIMU(gx, gy, gz, ax, ay, az);
        return;
    }

    if (!((ax == 0.0f) && (ay == 0.0f) && (az == 0.0f))) {
        recip_norm = mahony_inv_sqrt(ax * ax + ay * ay + az * az);
        ax *= recip_norm;
        ay *= recip_norm;
        az *= recip_norm;

        recip_norm = mahony_inv_sqrt(mx * mx + my * my + mz * mz);
        mx *= recip_norm;
        my *= recip_norm;
        mz *= recip_norm;

        q0q0 = s_q0 * s_q0;
        q0q1 = s_q0 * s_q1;
        q0q2 = s_q0 * s_q2;
        q0q3 = s_q0 * s_q3;
        q1q1 = s_q1 * s_q1;
        q1q2 = s_q1 * s_q2;
        q1q3 = s_q1 * s_q3;
        q2q2 = s_q2 * s_q2;
        q2q3 = s_q2 * s_q3;
        q3q3 = s_q3 * s_q3;

        hx = 2.0f * (mx * (0.5f - q2q2 - q3q3) + my * (q1q2 - q0q3) + mz * (q1q3 + q0q2));
        hy = 2.0f * (mx * (q1q2 + q0q3) + my * (0.5f - q1q1 - q3q3) + mz * (q2q3 - q0q1));
        bx = sqrtf(hx * hx + hy * hy);
        bz = 2.0f * (mx * (q1q3 - q0q2) + my * (q2q3 + q0q1) + mz * (0.5f - q1q1 - q2q2));

        half_vx = q1q3 - q0q2;
        half_vy = q0q1 + q2q3;
        half_vz = q0q0 - 0.5f + q3q3;
        half_wx = bx * (0.5f - q2q2 - q3q3) + bz * (q1q3 - q0q2);
        half_wy = bx * (q1q2 - q0q3) + bz * (q0q1 + q2q3);
        half_wz = bx * (q0q2 + q1q3) + bz * (0.5f - q1q1 - q2q2);

        half_ex = (ay * half_vz - az * half_vy) + (my * half_wz - mz * half_wy);
        half_ey = (az * half_vx - ax * half_vz) + (mz * half_wx - mx * half_wz);
        half_ez = (ax * half_vy - ay * half_vx) + (mx * half_wy - my * half_wx);

        if (s_two_ki > 0.0f) {
            s_integral_fb_x += s_two_ki * half_ex * s_inv_sample_freq;
            s_integral_fb_y += s_two_ki * half_ey * s_inv_sample_freq;
            s_integral_fb_z += s_two_ki * half_ez * s_inv_sample_freq;
            gx += s_integral_fb_x;
            gy += s_integral_fb_y;
            gz += s_integral_fb_z;
        } else {
            s_integral_fb_x = 0.0f;
            s_integral_fb_y = 0.0f;
            s_integral_fb_z = 0.0f;
        }

        gx += s_two_kp * half_ex;
        gy += s_two_kp * half_ey;
        gz += s_two_kp * half_ez;
    }

    gx *= 0.5f * s_inv_sample_freq;
    gy *= 0.5f * s_inv_sample_freq;
    gz *= 0.5f * s_inv_sample_freq;
    qa = s_q0;
    qb = s_q1;
    qc = s_q2;
    s_q0 += (-qb * gx - qc * gy - s_q3 * gz);
    s_q1 += (qa * gx + qc * gz - s_q3 * gy);
    s_q2 += (qa * gy - qb * gz + s_q3 * gx);
    s_q3 += (qa * gz + qb * gy - qc * gx);

    recip_norm = mahony_inv_sqrt(s_q0 * s_q0 + s_q1 * s_q1 + s_q2 * s_q2 + s_q3 * s_q3);
    s_q0 *= recip_norm;
    s_q1 *= recip_norm;
    s_q2 *= recip_norm;
    s_q3 *= recip_norm;
}

void MahonyAHRSupdateIMU(float gx, float gy, float gz, float ax, float ay, float az)
{
    float recip_norm;
    float half_vx;
    float half_vy;
    float half_vz;
    float half_ex;
    float half_ey;
    float half_ez;
    float qa;
    float qb;
    float qc;

    if (!((ax == 0.0f) && (ay == 0.0f) && (az == 0.0f))) {
        recip_norm = mahony_inv_sqrt(ax * ax + ay * ay + az * az);
        ax *= recip_norm;
        ay *= recip_norm;
        az *= recip_norm;

        half_vx = s_q1 * s_q3 - s_q0 * s_q2;
        half_vy = s_q0 * s_q1 + s_q2 * s_q3;
        half_vz = s_q0 * s_q0 - 0.5f + s_q3 * s_q3;

        half_ex = ay * half_vz - az * half_vy;
        half_ey = az * half_vx - ax * half_vz;
        half_ez = ax * half_vy - ay * half_vx;

        if (s_two_ki > 0.0f) {
            s_integral_fb_x += s_two_ki * half_ex * s_inv_sample_freq;
            s_integral_fb_y += s_two_ki * half_ey * s_inv_sample_freq;
            s_integral_fb_z += s_two_ki * half_ez * s_inv_sample_freq;
            gx += s_integral_fb_x;
            gy += s_integral_fb_y;
            gz += s_integral_fb_z;
        } else {
            s_integral_fb_x = 0.0f;
            s_integral_fb_y = 0.0f;
            s_integral_fb_z = 0.0f;
        }

        gx += s_two_kp * half_ex;
        gy += s_two_kp * half_ey;
        gz += s_two_kp * half_ez;
    }

    gx *= 0.5f * s_inv_sample_freq;
    gy *= 0.5f * s_inv_sample_freq;
    gz *= 0.5f * s_inv_sample_freq;
    qa = s_q0;
    qb = s_q1;
    qc = s_q2;
    s_q0 += (-qb * gx - qc * gy - s_q3 * gz);
    s_q1 += (qa * gx + qc * gz - s_q3 * gy);
    s_q2 += (qa * gy - qb * gz + s_q3 * gx);
    s_q3 += (qa * gz + qb * gy - qc * gx);

    recip_norm = mahony_inv_sqrt(s_q0 * s_q0 + s_q1 * s_q1 + s_q2 * s_q2 + s_q3 * s_q3);
    s_q0 *= recip_norm;
    s_q1 *= recip_norm;
    s_q2 *= recip_norm;
    s_q3 *= recip_norm;
}

static float mahony_inv_sqrt(float x)
{
    float halfx = 0.5f * x;
    float y = x;
    uint32_t i;

    if (x <= 0.0f) {
        return 0.0f;
    }

    memcpy(&i, &y, sizeof(i));
    i = 0x5f3759dfu - (i >> 1);
    memcpy(&y, &i, sizeof(y));
    y = y * (1.5f - (halfx * y * y));
    return y;
}
