/**
 * @file    attitude.c
 * @brief   九轴姿态解算（x-io Fusion 封装）
 *
 * 小车策略（docs/attitude-fusion.md §4 方案 B）：
 * - roll/pitch：Fusion 6-DOF（gyro+accel），不用 mag 参与四元数融合
 * - 静止时：accel 锁定 tilt，避免 roll/pitch 漂移
 * - yaw：FusionCompass 倾斜补偿罗盘；平放非快速转 yaw 时持续校正（对抗 gz 零偏）
 */

#include "attitude.h"

#include "FusionAhrs.h"
#include "FusionBias.h"
#include "FusionCompass.h"
#include "FusionRemap.h"
#include "nvs.h"

#include <math.h>

/** MPU6050：±2g */
#define ATTITUDE_ACCEL_LSB_PER_G 16384.0f
/** MPU6050：±250 °/s */
#define ATTITUDE_GYRO_LSB_PER_DPS 131.0f
#define ATTITUDE_GYRO_RANGE_DPS   250.0f

#define ATTITUDE_AHRS_GAIN              0.5f
#define ATTITUDE_ACCEL_REJECTION_DEG    10.0f
#define ATTITUDE_RECOVERY_SECONDS       5.0f
#define ATTITUDE_BIAS_STATIONARY_DPS    2.0f
#define ATTITUDE_BIAS_STATIONARY_SEC    3.0f

/** 合加速度接近 1g */
#define ATTITUDE_ACCEL_NORM_TOL_G       0.15f
/** |ω| 低于此值且 |a|≈1g 时用 accel 锁 roll/pitch、并允许 mag 校正 yaw */
#define ATTITUDE_TILT_LOCK_MAX_DPS      15.0f
/** |gz| 超过此值视为有意绕 yaw 转，暂停 mag 校正 */
#define ATTITUDE_MAG_YAW_SKIP_GZ_DPS    7.0f
/** |gx|/|gy| 超过此值视为在 roll/pitch 转，暂停 mag 校正 */
#define ATTITUDE_MAG_YAW_SKIP_XY_DPS    5.0f
/** mag yaw 融合系数（50Hz 下约 0.12 可抵消 ~2°/s 零偏漂移） */
#define ATTITUDE_MAG_YAW_ALPHA          0.12f
/** 快速绕 Z 转结束后，延迟若干帧再允许 mag 拉回 yaw（抑制停转瞬间反向跳变） */
#define ATTITUDE_MAG_YAW_COOLDOWN_FRAMES 25U
/** 角度环结束 HOLD 后，mag 融合系数渐变恢复帧数（50Hz 下约 500ms） */
#define ATTITUDE_YAW_HOLD_MAG_RAMP_FRAMES 25U

#define ATTITUDE_MAG_NORM_JUMP          0.22f
#define ATTITUDE_MAG_NORM_ALPHA         0.05f

#define ATTITUDE_EARTH_CONVENTION       FusionConventionNwu

/** 实机确认 PCB 朝向后修改（FusionRemap.h） */
#define ATTITUDE_IMU_REMAP              FusionRemapAlignmentPXPYPZ
#define ATTITUDE_MAG_REMAP              FusionRemapAlignmentPXPYPZ

static FusionAhrs s_ahrs;
static FusionBias s_bias;

static bool_t s_ready;
static bool_t s_tilt_seeded;
static bool_t s_mag_trust;
static bool_t s_mag_norm_ready;
static float s_mag_norm_ema;

static float s_prev_roll_deg;
static float s_prev_pitch_deg;
static float s_prev_yaw_deg;
static bool_t s_euler_prev_valid;
static float s_last_gz_dps;
static u8_t s_mag_yaw_cooldown;
static bool_t s_yaw_hold;
static u16_t s_yaw_mag_ramp_left;
static float s_sample_period;
static float s_yaw_ctrl_deg;
static bool_t s_yaw_ctrl_valid;

static FusionQuaternion attitude_quat_from_euler_rad(float roll, float pitch, float yaw)
{
    float cr = cosf(roll * 0.5f);
    float sr = sinf(roll * 0.5f);
    float cp = cosf(pitch * 0.5f);
    float sp = sinf(pitch * 0.5f);
    float cy = cosf(yaw * 0.5f);
    float sy = sinf(yaw * 0.5f);
    FusionQuaternion q;

    q.element.w = cr * cp * cy + sr * sp * sy;
    q.element.x = sr * cp * cy - cr * sp * sy;
    q.element.y = cr * sp * cy + sr * cp * sy;
    q.element.z = cr * cp * sy - sr * sp * cy;
    return q;
}

static bool_t attitude_tilt_rad_from_accel(FusionVector accelerometer, float *roll, float *pitch)
{
    float norm = FusionVectorNorm(accelerometer);

    if (norm < 0.5f) {
        return FALSE;
    }

    accelerometer = FusionVectorScale(accelerometer, 1.0f / norm);
    *roll = atan2f(accelerometer.axis.y, accelerometer.axis.z);
    *pitch = atan2f(-accelerometer.axis.x,
                     sqrtf((accelerometer.axis.y * accelerometer.axis.y) +
                           (accelerometer.axis.z * accelerometer.axis.z)));
    return TRUE;
}

static bool_t attitude_accel_is_gravity(FusionVector accelerometer)
{
    float norm = FusionVectorNorm(accelerometer);

    return fabsf(norm - 1.0f) <= ATTITUDE_ACCEL_NORM_TOL_G;
}

static float attitude_gyro_peak_dps(FusionVector gyroscope)
{
    float peak = fabsf(gyroscope.axis.x);

    if (fabsf(gyroscope.axis.y) > peak) {
        peak = fabsf(gyroscope.axis.y);
    }
    if (fabsf(gyroscope.axis.z) > peak) {
        peak = fabsf(gyroscope.axis.z);
    }
    return peak;
}

static FusionVector attitude_raw_to_gyro_dps(const imu_sample_t *imu)
{
    FusionVector v;

    v.axis.x = (float)imu->gx / ATTITUDE_GYRO_LSB_PER_DPS;
    v.axis.y = (float)imu->gy / ATTITUDE_GYRO_LSB_PER_DPS;
    v.axis.z = (float)imu->gz / ATTITUDE_GYRO_LSB_PER_DPS;
    return FusionRemap(v, ATTITUDE_IMU_REMAP);
}

static FusionVector attitude_raw_to_accel_g(const imu_sample_t *imu)
{
    FusionVector v;

    v.axis.x = (float)imu->ax / ATTITUDE_ACCEL_LSB_PER_G;
    v.axis.y = (float)imu->ay / ATTITUDE_ACCEL_LSB_PER_G;
    v.axis.z = (float)imu->az / ATTITUDE_ACCEL_LSB_PER_G;
    return FusionRemap(v, ATTITUDE_IMU_REMAP);
}

static FusionVector attitude_raw_to_mag(const magnetometer_sample_t *mag)
{
    FusionVector v;

    v.axis.x = (float)mag->mx;
    v.axis.y = (float)mag->my;
    v.axis.z = (float)mag->mz;
    return FusionRemap(v, ATTITUDE_MAG_REMAP);
}

static bool_t attitude_mag_sample_ok(int16_t mx, int16_t my, int16_t mz)
{
    float norm = sqrtf((float)mx * (float)mx + (float)my * (float)my + (float)mz * (float)mz);

    if (norm < 1.0f) {
        s_mag_trust = FALSE;
        return FALSE;
    }

    if (s_mag_norm_ready == FALSE) {
        s_mag_norm_ema = norm;
        s_mag_norm_ready = TRUE;
        s_mag_trust = TRUE;
        return TRUE;
    }

    if (fabsf(norm - s_mag_norm_ema) > (s_mag_norm_ema * ATTITUDE_MAG_NORM_JUMP)) {
        s_mag_trust = FALSE;
        return FALSE;
    }

    s_mag_norm_ema += ATTITUDE_MAG_NORM_ALPHA * (norm - s_mag_norm_ema);
    s_mag_trust = TRUE;
    return TRUE;
}

static void attitude_apply_fusion_settings(float sample_hz)
{
    FusionAhrsSettings ahrs_settings = {
        .sampleRate = sample_hz,
        .convention = ATTITUDE_EARTH_CONVENTION,
        .gain = ATTITUDE_AHRS_GAIN,
        .gyroscopeRange = ATTITUDE_GYRO_RANGE_DPS,
        .accelerationRejection = ATTITUDE_ACCEL_REJECTION_DEG,
        .magneticRejection = 0.0f,
        .recoveryTriggerPeriod = (unsigned int)(ATTITUDE_RECOVERY_SECONDS * sample_hz),
    };
    FusionBiasSettings bias_settings = {
        .sampleRate = sample_hz,
        .stationaryThreshold = ATTITUDE_BIAS_STATIONARY_DPS,
        .stationaryPeriod = ATTITUDE_BIAS_STATIONARY_SEC,
    };

    FusionAhrsInitialise(&s_ahrs);
    FusionAhrsSetSettings(&s_ahrs, &ahrs_settings);
    FusionBiasInitialise(&s_bias);
    FusionBiasSetSettings(&s_bias, &bias_settings);
}

static void attitude_load_nvs_gyro_offset(void)
{
    const nvs_cfg_t *cfg = nvs_cfg_get();
    FusionVector offset = {
        .axis.x = cfg->imu_offset.gyro[0],
        .axis.y = cfg->imu_offset.gyro[1],
        .axis.z = cfg->imu_offset.gyro[2],
    };

    FusionBiasSetOffset(&s_bias, offset);
}

static float attitude_wrap_deg_180(float deg)
{
    while (deg > 180.0f) {
        deg -= 360.0f;
    }
    while (deg < -180.0f) {
        deg += 360.0f;
    }
    return deg;
}

static int16_t attitude_deg_to_int16(float deg)
{
    if (deg >= 0.0f) {
        return (int16_t)(deg + 0.5f);
    }
    return (int16_t)(deg - 0.5f);
}

static int16_t attitude_deg_smooth_int(float deg, float *prev_deg)
{
    float d = attitude_wrap_deg_180(deg);
    float delta;
    float out;

    if (s_euler_prev_valid == FALSE) {
        out = d;
    } else {
        delta = d - *prev_deg;
        while (delta > 180.0f) {
            delta -= 360.0f;
        }
        while (delta < -180.0f) {
            delta += 360.0f;
        }
        out = attitude_wrap_deg_180(*prev_deg + delta);
    }

    *prev_deg = out;
    return attitude_deg_to_int16(out);
}

static void attitude_seed_tilt(FusionVector accelerometer, float yaw_deg)
{
    float roll;
    float pitch;
    FusionQuaternion q;

    if (attitude_tilt_rad_from_accel(accelerometer, &roll, &pitch) == FALSE) {
        return;
    }

    q = attitude_quat_from_euler_rad(roll, pitch, FusionDegreesToRadians(yaw_deg));
    FusionAhrsSetQuaternion(&s_ahrs, q);
    s_tilt_seeded = TRUE;
    s_euler_prev_valid = FALSE;
}

static void attitude_lock_tilt_preserve_yaw(FusionVector accelerometer)
{
    float roll;
    float pitch;
    float yaw_deg;
    FusionEuler euler;
    FusionQuaternion q;

    if (attitude_tilt_rad_from_accel(accelerometer, &roll, &pitch) == FALSE) {
        return;
    }

    euler = FusionQuaternionToEuler(FusionAhrsGetQuaternion(&s_ahrs));
    yaw_deg = euler.angle.yaw;
    q = attitude_quat_from_euler_rad(roll, pitch, FusionDegreesToRadians(yaw_deg));
    FusionAhrsSetQuaternion(&s_ahrs, q);
}

static void attitude_blend_yaw_from_compass(FusionVector accelerometer, FusionVector magnetometer,
                                            float alpha)
{
    float heading;
    float yaw_deg;
    float yaw_new;
    FusionEuler euler;

    if (alpha <= 0.0f) {
        return;
    }

    heading = FusionCompass(accelerometer, magnetometer, ATTITUDE_EARTH_CONVENTION);
    euler = FusionQuaternionToEuler(FusionAhrsGetQuaternion(&s_ahrs));
    yaw_deg = euler.angle.yaw;
    yaw_new = yaw_deg + (alpha * attitude_wrap_deg_180(heading - yaw_deg));
    FusionAhrsSetHeading(&s_ahrs, yaw_new);
}

static float attitude_mag_yaw_blend_alpha(void)
{
    float alpha = ATTITUDE_MAG_YAW_ALPHA;

    if (s_yaw_mag_ramp_left == 0U) {
        return alpha;
    }

    {
        u16_t elapsed = ATTITUDE_YAW_HOLD_MAG_RAMP_FRAMES - s_yaw_mag_ramp_left;

        alpha = ATTITUDE_MAG_YAW_ALPHA *
                ((float)elapsed / (float)ATTITUDE_YAW_HOLD_MAG_RAMP_FRAMES);
        s_yaw_mag_ramp_left--;
    }
    return alpha;
}

static bool_t attitude_can_blend_mag_yaw(FusionVector gyroscope, float gyro_peak_dps, bool_t gravity)
{
    if (s_yaw_hold != FALSE) {
        return FALSE;
    }
    if (s_mag_yaw_cooldown > 0U) {
        return FALSE;
    }
    if ((gravity == FALSE) || (gyro_peak_dps >= ATTITUDE_TILT_LOCK_MAX_DPS)) {
        return FALSE;
    }
    if ((fabsf(gyroscope.axis.x) >= ATTITUDE_MAG_YAW_SKIP_XY_DPS) ||
        (fabsf(gyroscope.axis.y) >= ATTITUDE_MAG_YAW_SKIP_XY_DPS)) {
        return FALSE;
    }
    if (fabsf(gyroscope.axis.z) >= ATTITUDE_MAG_YAW_SKIP_GZ_DPS) {
        return FALSE;
    }
    return TRUE;
}

static void attitude_update_fusion(const imu_sample_t *imu, const magnetometer_sample_t *mag)
{
    FusionVector gyroscope;
    FusionVector accelerometer;
    FusionVector magnetometer = FUSION_VECTOR_ZERO;
    float gyro_peak_dps;
    bool_t gravity;
    bool_t mag_yaw_ok;
    bool_t mag_ok = FALSE;

    FusionVector gyro_raw;

    gyro_raw = attitude_raw_to_gyro_dps(imu);
    accelerometer = attitude_raw_to_accel_g(imu);
    gyroscope = FusionBiasUpdate(&s_bias, gyro_raw);
    gyro_peak_dps = attitude_gyro_peak_dps(gyroscope);
    s_last_gz_dps = gyroscope.axis.z;
    if (fabsf(gyroscope.axis.z) >= ATTITUDE_MAG_YAW_SKIP_GZ_DPS) {
        s_mag_yaw_cooldown = ATTITUDE_MAG_YAW_COOLDOWN_FRAMES;
    } else if (s_mag_yaw_cooldown > 0U) {
        s_mag_yaw_cooldown--;
    }
    gravity = attitude_accel_is_gravity(accelerometer);

    if (mag != NULL) {
        mag_ok = attitude_mag_sample_ok(mag->mx, mag->my, mag->mz);
        if (mag_ok != FALSE) {
            magnetometer = attitude_raw_to_mag(mag);
        }
    }

    if (s_tilt_seeded == FALSE) {
        float yaw_seed = 0.0f;

        if (mag_ok != FALSE) {
            yaw_seed = FusionCompass(accelerometer, magnetometer, ATTITUDE_EARTH_CONVENTION);
        }
        attitude_seed_tilt(accelerometer, yaw_seed);
    }

    /* 6-DOF：mag 不参与四元数融合，避免磁干扰带动 roll/pitch 绕圈漂移 */
    FusionAhrsUpdateNoMagnetometer(&s_ahrs, gyroscope, accelerometer);

    if (s_yaw_hold != FALSE) {
        s_yaw_ctrl_deg = attitude_wrap_deg_180(s_yaw_ctrl_deg + (gyro_raw.axis.z * s_sample_period));
        s_prev_yaw_deg = s_yaw_ctrl_deg;
    }

    if ((s_yaw_hold == FALSE) && gravity && (gyro_peak_dps < ATTITUDE_TILT_LOCK_MAX_DPS)) {
        attitude_lock_tilt_preserve_yaw(accelerometer);
    }

    mag_yaw_ok = attitude_can_blend_mag_yaw(gyroscope, gyro_peak_dps, gravity);
    if (mag_yaw_ok && (mag_ok != FALSE)) {
        float alpha = attitude_mag_yaw_blend_alpha();

        attitude_blend_yaw_from_compass(accelerometer, magnetometer, alpha);
        s_mag_trust = TRUE;
    } else if (mag_ok != FALSE) {
        s_mag_trust = TRUE;
    }
}

status_t attitude_init(float sample_hz)
{
    if (sample_hz <= 0.0f) {
        return STATUS_INVALID_ARG;
    }

    attitude_apply_fusion_settings(sample_hz);
    attitude_load_nvs_gyro_offset();

    s_sample_period = 1.0f / sample_hz;
    s_tilt_seeded = FALSE;
    s_mag_norm_ema = 0.0f;
    s_mag_norm_ready = FALSE;
    s_mag_trust = TRUE;
    s_euler_prev_valid = FALSE;
    s_prev_roll_deg = 0.0f;
    s_prev_pitch_deg = 0.0f;
    s_prev_yaw_deg = 0.0f;
    s_last_gz_dps = 0.0f;
    s_mag_yaw_cooldown = 0U;
    s_yaw_hold = FALSE;
    s_yaw_mag_ramp_left = 0U;
    s_yaw_ctrl_deg = 0.0f;
    s_yaw_ctrl_valid = FALSE;
    s_ready = TRUE;
    return STATUS_OK;
}

bool_t attitude_is_ready(void)
{
    return s_ready;
}

status_t attitude_get_status(attitude_status_t *status)
{
    if (status == NULL) {
        return STATUS_INVALID_ARG;
    }

    status->mag_trust = s_mag_trust;
    status->gyro_bias_ready = (s_bias.timer >= s_bias.timeout) ? TRUE : FALSE;
    return STATUS_OK;
}

status_t attitude_update_from_imu(const imu_sample_t *imu)
{
    if (s_ready == FALSE) {
        return STATUS_INVALID_STATE;
    }
    if (imu == NULL) {
        return STATUS_INVALID_ARG;
    }

    attitude_update_fusion(imu, NULL);
    return STATUS_OK;
}

status_t attitude_update_from_sensors(const imu_sample_t *imu, const magnetometer_sample_t *mag)
{
    if (s_ready == FALSE) {
        return STATUS_INVALID_STATE;
    }
    if ((imu == NULL) || (mag == NULL)) {
        return STATUS_INVALID_ARG;
    }

    attitude_update_fusion(imu, mag);
    return STATUS_OK;
}

status_t attitude_update_step(const imu_sample_t *imu, const magnetometer_sample_t *mag)
{
    if (s_ready == FALSE) {
        return STATUS_INVALID_STATE;
    }
    if (imu == NULL) {
        return STATUS_INVALID_ARG;
    }

    attitude_update_fusion(imu, mag);
    return STATUS_OK;
}

status_t attitude_get_euler(attitude_euler_t *euler)
{
    FusionQuaternion quat;
    FusionEuler angles;

    if (s_ready == FALSE) {
        return STATUS_INVALID_STATE;
    }
    if (euler == NULL) {
        return STATUS_INVALID_ARG;
    }

    quat = FusionAhrsGetQuaternion(&s_ahrs);
    angles = FusionQuaternionToEuler(quat);

    euler->roll = attitude_deg_smooth_int(angles.angle.roll, &s_prev_roll_deg);
    euler->pitch = attitude_deg_smooth_int(angles.angle.pitch, &s_prev_pitch_deg);
    if ((s_yaw_hold != FALSE) && (s_yaw_ctrl_valid != FALSE)) {
        euler->yaw = attitude_deg_to_int16(s_yaw_ctrl_deg);
    } else {
        euler->yaw = attitude_deg_smooth_int(angles.angle.yaw, &s_prev_yaw_deg);
    }
    s_euler_prev_valid = TRUE;
    return STATUS_OK;
}

status_t attitude_get_yaw_control(float *yaw_deg, float *yaw_rate_dps)
{
    if (s_ready == FALSE) {
        return STATUS_INVALID_STATE;
    }

    if (yaw_deg != NULL) {
        if ((s_yaw_hold != FALSE) && (s_yaw_ctrl_valid != FALSE)) {
            *yaw_deg = s_yaw_ctrl_deg;
        } else {
            *yaw_deg = s_prev_yaw_deg;
        }
    }
    if (yaw_rate_dps != NULL) {
        *yaw_rate_dps = s_last_gz_dps;
    }
    return STATUS_OK;
}

void attitude_yaw_hold_set(bool_t active)
{
    if (active != FALSE) {
        s_yaw_ctrl_deg = s_prev_yaw_deg;
        s_yaw_ctrl_valid = TRUE;
        s_yaw_hold = TRUE;
        s_yaw_mag_ramp_left = 0U;
        return;
    }

    if (s_yaw_hold == FALSE) {
        return;
    }

    if (s_yaw_ctrl_valid != FALSE) {
        FusionAhrsSetHeading(&s_ahrs, s_yaw_ctrl_deg);
        s_prev_yaw_deg = s_yaw_ctrl_deg;
    }

    s_yaw_hold = FALSE;
    s_yaw_ctrl_valid = FALSE;
    s_yaw_mag_ramp_left = ATTITUDE_YAW_HOLD_MAG_RAMP_FRAMES;
    if (s_mag_yaw_cooldown < ATTITUDE_MAG_YAW_COOLDOWN_FRAMES) {
        s_mag_yaw_cooldown = ATTITUDE_MAG_YAW_COOLDOWN_FRAMES;
    }
}

bool_t attitude_yaw_hold_is_active(void)
{
    return s_yaw_hold;
}
