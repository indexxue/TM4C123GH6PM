/**
 * @file    chassis.c
 * @brief   四轮速度 PID 内环
 */

#include "chassis.h"

#include "board.h"
#include "cfg.h"
#include "device_profile.h"
#include "motion.h"
#include "nvs.h"
#include "pid.h"

#include <math.h>

#define CHASSIS_MOTOR_COUNT 4U
#define CHASSIS_PID_OUT_MAX_PERMILLE 400.0f
#define CHASSIS_PID_INTEGRAL_MAX     300.0f

#ifndef CHASSIS_PID_SIGN_FIX_ENABLE
/** 1=目标/反馈异号时改按 |RPM| 闭环，方向仍由 target 符号决定 */
#define CHASSIS_PID_SIGN_FIX_ENABLE  1
#endif

#ifndef CHASSIS_PID_DUTY_FLOOR_GUARD
/** 1=非零目标时占空不低于起转阈值，防止 PID 把输出砍到 0 */
#define CHASSIS_PID_DUTY_FLOOR_GUARD  1
#endif

#ifndef CHASSIS_PID_SIGN_FIX_MIN_RPM
#define CHASSIS_PID_SIGN_FIX_MIN_RPM  5.0f
#endif

static pid_t s_pid[CHASSIS_MOTOR_COUNT];
static f32_t s_target_rpm[CHASSIS_MOTOR_COUNT];
static f32_t s_ramped_rpm[CHASSIS_MOTOR_COUNT];
static s8_t s_target_sign[CHASSIS_MOTOR_COUNT];
static bool_t s_active;

void chassis_reload_pid_gains(void)
{
    const nvs_pid3_t *g = cfg_pid_speed();
    u8_t i;

    if (g == NULL) {
        return;
    }

    for (i = 0U; i < CHASSIS_MOTOR_COUNT; i++) {
        pid_set_gains(&s_pid[i], g->kp, g->ki, g->kd);
    }
}

static void chassis_reset_targets(void)
{
    u8_t i;

    for (i = 0U; i < CHASSIS_MOTOR_COUNT; i++) {
        s_target_rpm[i] = 0.0f;
        s_ramped_rpm[i] = 0.0f;
        s_target_sign[i] = 0;
    }
}

static f32_t chassis_clamp_rpm(f32_t rpm)
{
    const nvs_spd_limit_t *lim = cfg_spd_limit();
    f32_t max_rpm = (lim != NULL) ? lim->max_rpm : 300.0f;

    if (max_rpm <= 0.0f) {
        max_rpm = 300.0f;
    }
    if (rpm > max_rpm) {
        return max_rpm;
    }
    if (rpm < -max_rpm) {
        return -max_rpm;
    }
    return rpm;
}

static void chassis_ramp_targets(f32_t dt_s)
{
    const nvs_spd_limit_t *lim = cfg_spd_limit();
    f32_t max_step;
    u8_t i;

    if (lim == NULL) {
        return;
    }

    max_step = lim->max_accel_rpm_s * dt_s;
    if (max_step <= 0.0f) {
        for (i = 0U; i < CHASSIS_MOTOR_COUNT; i++) {
            s_ramped_rpm[i] = s_target_rpm[i];
        }
        return;
    }

    for (i = 0U; i < CHASSIS_MOTOR_COUNT; i++) {
        f32_t err = s_target_rpm[i] - s_ramped_rpm[i];

        if (err > max_step) {
            s_ramped_rpm[i] += max_step;
        } else if (err < -max_step) {
            s_ramped_rpm[i] -= max_step;
        } else {
            s_ramped_rpm[i] = s_target_rpm[i];
        }
    }
}

/**
 * 速度环按 |RPM| 闭环，方向仅由 target 符号经 Motor_SetOutput 决定。
 * 有符号 PID 在负目标时误差恒为负，会把占空压死（+100 正常、-100 跑 ~2s 后停）。
 */
static void chassis_pid_setpoint_process(f32_t target_rpm, f32_t measured_rpm, f32_t *sp_out,
                                         f32_t *pv_out)
{
#if CHASSIS_PID_SIGN_FIX_ENABLE
    if (fabsf(target_rpm) >= CHASSIS_PID_SIGN_FIX_MIN_RPM) {
        *sp_out = fabsf(target_rpm);
        *pv_out = fabsf(measured_rpm);
        return;
    }
#endif
    *sp_out = target_rpm;
    *pv_out = measured_rpm;
}

static s8_t chassis_target_sign(f32_t target_rpm)
{
    if (target_rpm > 0.5f) {
        return 1;
    }
    if (target_rpm < -0.5f) {
        return -1;
    }
    return 0;
}

static void chassis_pid_reset_on_dir_change(u8_t motor_id, f32_t target_rpm)
{
    s8_t sign = chassis_target_sign(target_rpm);
    u8_t idx = motor_id - 1U;

    if ((sign != 0) && (s_target_sign[idx] != 0) && (sign != s_target_sign[idx])) {
        pid_reset(&s_pid[idx]);
    }
    if (sign != 0) {
        s_target_sign[idx] = sign;
    } else {
        s_target_sign[idx] = 0;
    }
}

static void chassis_motor_output(u8_t motor_id, f32_t target_rpm, f32_t measured_rpm, f32_t dt_s)
{
    f32_t pid_out;
    f32_t duty_f;
    u16_t duty_ff;
    u16_t duty;
    s32_t dir_rpm;
    f32_t pid_sp;
    f32_t pid_pv;

    if (fabsf(target_rpm) < 1.0f) {
        target_rpm = 0.0f;
    }

    chassis_pid_reset_on_dir_change(motor_id, target_rpm);
    chassis_pid_setpoint_process(target_rpm, measured_rpm, &pid_sp, &pid_pv);
    pid_out = pid_update(&s_pid[motor_id - 1U], pid_sp, pid_pv, dt_s);
    duty_ff = motion_rpm_to_duty_permille(fabsf(target_rpm));
    duty_f = (f32_t)duty_ff + pid_out;

    if (duty_f < 0.0f) {
        duty_f = 0.0f;
    }
    if (duty_f > 1000.0f) {
        duty_f = 1000.0f;
    }

#if CHASSIS_PID_DUTY_FLOOR_GUARD
    if ((fabsf(target_rpm) >= CHASSIS_PID_SIGN_FIX_MIN_RPM) && (duty_ff > 0U) &&
        (duty_f < (f32_t)MOTION_MIN_DUTY_PERMILLE)) {
        duty_f = (f32_t)MOTION_MIN_DUTY_PERMILLE;
    }
#endif

    duty = (u16_t)duty_f;

    if (target_rpm == 0.0f) {
        duty = 0U;
    }

    dir_rpm = (s32_t)target_rpm;
    Motor_SetOutput(motor_id, cfg_motor_rpm(motor_id, dir_rpm), duty);
}

void chassis_init(void)
{
    u8_t i;

    chassis_reset_targets();
    s_active = FALSE;

    for (i = 0U; i < CHASSIS_MOTOR_COUNT; i++) {
        pid_init(&s_pid[i], 1.0f, 0.0f, 0.0f);
        pid_set_output_limits(&s_pid[i], -CHASSIS_PID_OUT_MAX_PERMILLE, CHASSIS_PID_OUT_MAX_PERMILLE);
        pid_set_integral_limit(&s_pid[i], CHASSIS_PID_INTEGRAL_MAX);
    }
    chassis_reload_pid_gains();
    motion_init();
}

void chassis_stop(void)
{
    u8_t i;

    chassis_reset_targets();
    s_active = FALSE;

    for (i = 0U; i < CHASSIS_MOTOR_COUNT; i++) {
        pid_reset(&s_pid[i]);
    }

    if (device_profile_board_wants(DEVICE_BOARD_MASK_MOTOR)) {
        for (i = 1U; i <= CHASSIS_MOTOR_COUNT; i++) {
            Motor_SetOutput(i, 0, 0U);
        }
    }
}

bool_t chassis_is_active(void)
{
    return s_active;
}

void chassis_set_wheel_rpm(u8_t motor_id, s32_t rpm)
{
    if ((motor_id < 1U) || (motor_id > CHASSIS_MOTOR_COUNT)) {
        return;
    }

    s_target_rpm[motor_id - 1U] = chassis_clamp_rpm((f32_t)rpm);
    s_active = TRUE;
}

void chassis_set_lr_rpm(s32_t left_rpm, s32_t right_rpm)
{
    left_rpm = (s32_t)chassis_clamp_rpm((f32_t)left_rpm);
    right_rpm = (s32_t)chassis_clamp_rpm((f32_t)right_rpm);

    s_target_rpm[0] = (f32_t)left_rpm;
    s_target_rpm[2] = (f32_t)left_rpm;
    s_target_rpm[1] = (f32_t)right_rpm;
    s_target_rpm[3] = (f32_t)right_rpm;
    s_active = TRUE;
}

void chassis_set_drive(s32_t throttle, s32_t steer, s32_t throttle_max, s32_t steer_max)
{
    const nvs_spd_limit_t *lim = cfg_spd_limit();
    f32_t max_rpm;
    f32_t base_rpm;
    f32_t turn_rpm;
    s32_t left;
    s32_t right;

    if (throttle_max <= 0) {
        throttle_max = 1000;
    }
    if (steer_max <= 0) {
        steer_max = 1000;
    }

    max_rpm = (lim != NULL) ? lim->max_rpm : 300.0f;
    base_rpm = ((f32_t)throttle / (f32_t)throttle_max) * max_rpm;
    turn_rpm = ((f32_t)steer / (f32_t)steer_max) * max_rpm * 0.5f;
    left = (s32_t)(base_rpm - turn_rpm);
    right = (s32_t)(base_rpm + turn_rpm);
    chassis_set_lr_rpm(left, right);
}

s32_t chassis_get_wheel_rpm(u8_t motor_id)
{
    f32_t rpm;

    if ((motor_id < 1U) || (motor_id > CHASSIS_MOTOR_COUNT)) {
        return 0;
    }

    rpm = motion_get_logical_rpm(motor_id);
    if (rpm >= 0.0f) {
        return (s32_t)(rpm + 0.5f);
    }
    return (s32_t)(rpm - 0.5f);
}

void chassis_tick(u32_t period_ms)
{
    f32_t dt_s;
    u8_t i;

    if (!device_profile_board_wants(DEVICE_BOARD_MASK_MOTOR | DEVICE_BOARD_MASK_ENCODER)) {
        return;
    }

    motion_update(period_ms);

    if (period_ms == 0U) {
        return;
    }
    dt_s = (f32_t)period_ms / 1000.0f;

    chassis_ramp_targets(dt_s);

    if (!s_active) {
        for (i = 1U; i <= CHASSIS_MOTOR_COUNT; i++) {
            Motor_SetOutput(i, 0, 0U);
        }
        return;
    }

    for (i = 1U; i <= CHASSIS_MOTOR_COUNT; i++) {
        f32_t target = s_ramped_rpm[i - 1U];
        f32_t measured = motion_get_logical_rpm(i);

        chassis_motor_output(i, target, measured, dt_s);
    }
}
