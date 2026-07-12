/**
 * @file    MadgwickAHRS.c
 * @brief   Madgwick IMU/AHRS sensor fusion (Sebastian Madgwick, GPL-3.0)
 * @see     https://x-io.co.uk/open-source-imu-and-ahrs-algorithms/
 */

#include "MadgwickAHRS.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

#define MADGWICK_BETA_DEFAULT 0.1f

static float s_beta = MADGWICK_BETA_DEFAULT;
static float s_inv_sample_freq = 1.0f / 50.0f;
static float s_q0 = 1.0f;
static float s_q1 = 0.0f;
static float s_q2 = 0.0f;
static float s_q3 = 0.0f;

static float madgwick_inv_sqrt(float x);

void MadgwickAHRS_init(float sample_hz, float beta_gain)
{
    if (sample_hz > 0.0f) {
        s_inv_sample_freq = 1.0f / sample_hz;
    }
    s_beta = beta_gain;
    MadgwickAHRS_reset();
}

void MadgwickAHRS_set_beta(float beta_gain)
{
    s_beta = beta_gain;
}

void MadgwickAHRS_reset(void)
{
    s_q0 = 1.0f;
    s_q1 = 0.0f;
    s_q2 = 0.0f;
    s_q3 = 0.0f;
}

bool MadgwickAHRS_get_euler_deg(float *roll_deg, float *pitch_deg, float *yaw_deg)
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

void MadgwickAHRSupdate(float gx, float gy, float gz, float ax, float ay, float az, float mx, float my,
                        float mz)
{
    float recip_norm;
    float s0;
    float s1;
    float s2;
    float s3;
    float q_dot1;
    float q_dot2;
    float q_dot3;
    float q_dot4;
    float hx;
    float hy;
    float _2q0mx;
    float _2q0my;
    float _2q0mz;
    float _2q1mx;
    float _2bx;
    float _2bz;
    float _4bx;
    float _4bz;
    float _2q0;
    float _2q1;
    float _2q2;
    float _2q3;
    float _2q0q2;
    float _2q2q3;
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

    if ((mx == 0.0f) && (my == 0.0f) && (mz == 0.0f)) {
        MadgwickAHRSupdateIMU(gx, gy, gz, ax, ay, az);
        return;
    }

    q_dot1 = 0.5f * (-s_q1 * gx - s_q2 * gy - s_q3 * gz);
    q_dot2 = 0.5f * (s_q0 * gx + s_q2 * gz - s_q3 * gy);
    q_dot3 = 0.5f * (s_q0 * gy - s_q1 * gz + s_q3 * gx);
    q_dot4 = 0.5f * (s_q0 * gz + s_q1 * gy - s_q2 * gx);

    if (!((ax == 0.0f) && (ay == 0.0f) && (az == 0.0f))) {
        recip_norm = madgwick_inv_sqrt(ax * ax + ay * ay + az * az);
        ax *= recip_norm;
        ay *= recip_norm;
        az *= recip_norm;

        recip_norm = madgwick_inv_sqrt(mx * mx + my * my + mz * mz);
        mx *= recip_norm;
        my *= recip_norm;
        mz *= recip_norm;

        _2q0mx = 2.0f * s_q0 * mx;
        _2q0my = 2.0f * s_q0 * my;
        _2q0mz = 2.0f * s_q0 * mz;
        _2q1mx = 2.0f * s_q1 * mx;
        _2q0 = 2.0f * s_q0;
        _2q1 = 2.0f * s_q1;
        _2q2 = 2.0f * s_q2;
        _2q3 = 2.0f * s_q3;
        _2q0q2 = 2.0f * s_q0 * s_q2;
        _2q2q3 = 2.0f * s_q2 * s_q3;
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

        hx = mx * q0q0 - _2q0my * s_q3 + _2q0mz * s_q2 + mx * q1q1 + _2q1 * my * s_q2 + _2q1 * mz * s_q3 -
             mx * q2q2 - mx * q3q3;
        hy = _2q0mx * s_q3 + my * q0q0 - _2q0mz * s_q1 + _2q1mx * s_q2 - my * q1q1 + my * q2q2 + _2q2 * mz * s_q3 -
             my * q3q3;
        _2bx = sqrtf(hx * hx + hy * hy);
        _2bz = -_2q0mx * s_q2 + _2q0my * s_q1 + mz * q0q0 + _2q1mx * s_q3 - mz * q1q1 + _2q2 * my * s_q3 -
               mz * q2q2 + mz * q3q3;
        _4bx = 2.0f * _2bx;
        _4bz = 2.0f * _2bz;

        s0 = -_2q2 * (2.0f * q1q3 - _2q0q2 - ax) + _2q1 * (2.0f * q0q1 + _2q2q3 - ay) -
             _2bz * s_q2 * (_2bx * (0.5f - q2q2 - q3q3) + _2bz * (q1q3 - q0q2) - mx) +
             (-_2bx * s_q3 + _2bz * s_q1) * (_2bx * (q1q2 - q0q3) + _2bz * (q0q1 + q2q3) - my) +
             _2bx * s_q2 * (_2bx * (q0q2 + q1q3) + _2bz * (0.5f - q1q1 - q2q2) - mz);
        s1 = _2q3 * (2.0f * q1q3 - _2q0q2 - ax) + _2q0 * (2.0f * q0q1 + _2q2q3 - ay) -
             4.0f * s_q1 * (1.0f - 2.0f * q1q1 - 2.0f * q2q2 - az) +
             _2bz * s_q3 * (_2bx * (0.5f - q2q2 - q3q3) + _2bz * (q1q3 - q0q2) - mx) +
             (_2bx * s_q2 + _2bz * s_q0) * (_2bx * (q1q2 - q0q3) + _2bz * (q0q1 + q2q3) - my) +
             (_2bx * s_q3 - _4bz * s_q1) * (_2bx * (q0q2 + q1q3) + _2bz * (0.5f - q1q1 - q2q2) - mz);
        s2 = -_2q0 * (2.0f * q1q3 - _2q0q2 - ax) + _2q3 * (2.0f * q0q1 + _2q2q3 - ay) -
             4.0f * s_q2 * (1.0f - 2.0f * q1q1 - 2.0f * q2q2 - az) +
             (-_4bx * s_q2 - _2bz * s_q0) * (_2bx * (0.5f - q2q2 - q3q3) + _2bz * (q1q3 - q0q2) - mx) +
             (_2bx * s_q1 + _2bz * s_q3) * (_2bx * (q1q2 - q0q3) + _2bz * (q0q1 + q2q3) - my) +
             (_2bx * s_q0 - _4bz * s_q2) * (_2bx * (q0q2 + q1q3) + _2bz * (0.5f - q1q1 - q2q2) - mz);
        s3 = _2q1 * (2.0f * q1q3 - _2q0q2 - ax) + _2q2 * (2.0f * q0q1 + _2q2q3 - ay) +
             (-_4bx * s_q3 + _2bz * s_q1) * (_2bx * (0.5f - q2q2 - q3q3) + _2bz * (q1q3 - q0q2) - mx) +
             (-_2bx * s_q0 + _2bz * s_q2) * (_2bx * (q1q2 - q0q3) + _2bz * (q0q1 + q2q3) - my) +
             _2bx * s_q1 * (_2bx * (q0q2 + q1q3) + _2bz * (0.5f - q1q1 - q2q2) - mz);
        recip_norm = madgwick_inv_sqrt(s0 * s0 + s1 * s1 + s2 * s2 + s3 * s3);
        s0 *= recip_norm;
        s1 *= recip_norm;
        s2 *= recip_norm;
        s3 *= recip_norm;

        q_dot1 -= s_beta * s0;
        q_dot2 -= s_beta * s1;
        q_dot3 -= s_beta * s2;
        q_dot4 -= s_beta * s3;
    }

    s_q0 += q_dot1 * s_inv_sample_freq;
    s_q1 += q_dot2 * s_inv_sample_freq;
    s_q2 += q_dot3 * s_inv_sample_freq;
    s_q3 += q_dot4 * s_inv_sample_freq;

    recip_norm = madgwick_inv_sqrt(s_q0 * s_q0 + s_q1 * s_q1 + s_q2 * s_q2 + s_q3 * s_q3);
    s_q0 *= recip_norm;
    s_q1 *= recip_norm;
    s_q2 *= recip_norm;
    s_q3 *= recip_norm;
}

void MadgwickAHRSupdateIMU(float gx, float gy, float gz, float ax, float ay, float az)
{
    float recip_norm;
    float s0;
    float s1;
    float s2;
    float s3;
    float q_dot1;
    float q_dot2;
    float q_dot3;
    float q_dot4;
    float _2q0;
    float _2q1;
    float _2q2;
    float _2q3;
    float _4q0;
    float _4q1;
    float _4q2;
    float _8q1;
    float _8q2;
    float q0q0;
    float q1q1;
    float q2q2;
    float q3q3;

    q_dot1 = 0.5f * (-s_q1 * gx - s_q2 * gy - s_q3 * gz);
    q_dot2 = 0.5f * (s_q0 * gx + s_q2 * gz - s_q3 * gy);
    q_dot3 = 0.5f * (s_q0 * gy - s_q1 * gz + s_q3 * gx);
    q_dot4 = 0.5f * (s_q0 * gz + s_q1 * gy - s_q2 * gx);

    if (!((ax == 0.0f) && (ay == 0.0f) && (az == 0.0f))) {
        recip_norm = madgwick_inv_sqrt(ax * ax + ay * ay + az * az);
        ax *= recip_norm;
        ay *= recip_norm;
        az *= recip_norm;

        _2q0 = 2.0f * s_q0;
        _2q1 = 2.0f * s_q1;
        _2q2 = 2.0f * s_q2;
        _2q3 = 2.0f * s_q3;
        _4q0 = 4.0f * s_q0;
        _4q1 = 4.0f * s_q1;
        _4q2 = 4.0f * s_q2;
        _8q1 = 8.0f * s_q1;
        _8q2 = 8.0f * s_q2;
        q0q0 = s_q0 * s_q0;
        q1q1 = s_q1 * s_q1;
        q2q2 = s_q2 * s_q2;
        q3q3 = s_q3 * s_q3;

        s0 = _4q0 * q2q2 + _2q2 * ax + _4q0 * q1q1 - _2q1 * ay;
        s1 = _4q1 * q3q3 - _2q3 * ax + 4.0f * q0q0 * s_q1 - _2q0 * ay - _4q1 + _8q1 * q1q1 + _8q1 * q2q2 +
             _4q1 * az;
        s2 = 4.0f * q0q0 * s_q2 + _2q0 * ax + _4q2 * q3q3 - _2q3 * ay - _4q2 + _8q2 * q1q1 + _8q2 * q2q2 +
             _4q2 * az;
        s3 = 4.0f * q1q1 * s_q3 - _2q1 * ax + 4.0f * q2q2 * s_q3 - _2q2 * ay;
        recip_norm = madgwick_inv_sqrt(s0 * s0 + s1 * s1 + s2 * s2 + s3 * s3);
        s0 *= recip_norm;
        s1 *= recip_norm;
        s2 *= recip_norm;
        s3 *= recip_norm;

        q_dot1 -= s_beta * s0;
        q_dot2 -= s_beta * s1;
        q_dot3 -= s_beta * s2;
        q_dot4 -= s_beta * s3;
    }

    s_q0 += q_dot1 * s_inv_sample_freq;
    s_q1 += q_dot2 * s_inv_sample_freq;
    s_q2 += q_dot3 * s_inv_sample_freq;
    s_q3 += q_dot4 * s_inv_sample_freq;

    recip_norm = madgwick_inv_sqrt(s_q0 * s_q0 + s_q1 * s_q1 + s_q2 * s_q2 + s_q3 * s_q3);
    s_q0 *= recip_norm;
    s_q1 *= recip_norm;
    s_q2 *= recip_norm;
    s_q3 *= recip_norm;
}

static float madgwick_inv_sqrt(float x)
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
