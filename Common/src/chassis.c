/**
 * @file    chassis.c
 * @brief   速度 PID 内环 + 航向角外环（2wd / 4wd）
 */

#include "chassis.h"

#include "attitude.h"
#include "board.h"
#include "cfg.h"
#include "device_profile.h"
#include "motion.h"
#include "nvs.h"
#include "pid.h"

#include "FreeRTOS.h"
#include "semphr.h"

#include <math.h>

#define CHASSIS_MOTOR_COUNT 4U
#define CHASSIS_PID_OUT_MAX_PERMILLE 400.0f
#define CHASSIS_PID_INTEGRAL_MAX     300.0f
#define CHASSIS_ANGLE_PID_OUT_MAX_RPM 200.0f
#define CHASSIS_ANGLE_PID_INTEGRAL_MAX 120.0f
#define CHASSIS_DISTANCE_PID_OUT_MAX_RPM 300.0f
#define CHASSIS_DISTANCE_PID_INTEGRAL_MAX 150.0f

#ifndef CHASSIS_ANGLE_DEADBAND_DEG
/** |误差| 低于此值视为到位，停差速并清积分 */
#define CHASSIS_ANGLE_DEADBAND_DEG    2.5f
#endif

#ifndef CHASSIS_ANGLE_YAW_LPF_ALPHA
/** 反馈 yaw 一阶低通，抑制 mag 停转拉回造成的单帧反向跳变 */
#define CHASSIS_ANGLE_YAW_LPF_ALPHA   0.35f
#endif

#ifndef CHASSIS_ANGLE_GZ_DAMP_SCALE
/** Kd × gz(°/s) → turn RPM 阻尼系数 */
#define CHASSIS_ANGLE_GZ_DAMP_SCALE   0.12f
#endif

#ifndef CHASSIS_DISTANCE_DEADBAND_MM
/** |误差| 低于此值视为到位并停车 */
#define CHASSIS_DISTANCE_DEADBAND_MM   8.0f
#endif

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

#ifndef CHASSIS_FF_GAIN
/** 前馈占空缩放：<1 减轻模型偏乐观导致的稳态偏高，余量交给 PI */
#define CHASSIS_FF_GAIN  0.90f
#endif

typedef enum {
    CHASSIS_CTRL_IDLE = 0,
    CHASSIS_CTRL_SPEED,
    CHASSIS_CTRL_ANGLE,
    CHASSIS_CTRL_DISTANCE,
} chassis_ctrl_mode_t;

static pid_t s_pid[CHASSIS_MOTOR_COUNT];
static pid_t s_pid_yaw;
static pid_t s_pid_dist;
static SemaphoreHandle_t s_chassis_mutex;
static f32_t s_target_rpm[CHASSIS_MOTOR_COUNT];
static f32_t s_ramped_rpm[CHASSIS_MOTOR_COUNT];
static s8_t s_target_sign[CHASSIS_MOTOR_COUNT];
static bool_t s_active;
static chassis_ctrl_mode_t s_ctrl_mode;

static s16_t s_angle_target_yaw;
static f32_t s_angle_target_yaw_f;
static s32_t s_angle_base_rpm;
static s32_t s_angle_max_turn_rpm;
static s16_t s_angle_current_yaw;
static s32_t s_angle_turn_rpm;
static f32_t s_angle_yaw_filt;
static bool_t s_angle_yaw_filt_valid;
static f32_t s_angle_odom_yaw;
static bool_t s_angle_odom_valid;
static int32_t s_odom_enc_prev[CHASSIS_MOTOR_COUNT];

static s32_t s_dist_target_mm;
static f32_t s_dist_target_mm_f;
static s32_t s_dist_max_rpm;
static f32_t s_dist_odom_mm;
static bool_t s_dist_odom_valid;
static s32_t s_dist_current_mm;
static s32_t s_dist_cmd_rpm;

static void chassis_apply_lr_rpm(s32_t left_rpm, s32_t right_rpm);
static void chassis_distance_clear_state(void);
static void chassis_reset_targets(void);
static void chassis_lock(void);
static void chassis_unlock(void);

static void chassis_lock(void)
{
    if (s_chassis_mutex != NULL) {
        (void)xSemaphoreTake(s_chassis_mutex, portMAX_DELAY);
    }
}

static void chassis_unlock(void)
{
    if (s_chassis_mutex != NULL) {
        (void)xSemaphoreGive(s_chassis_mutex);
    }
}

static bool_t chassis_distance_reached(void)
{
    if (fabsf(s_dist_target_mm_f) <= CHASSIS_DISTANCE_DEADBAND_MM) {
        return (fabsf(s_dist_odom_mm) <= CHASSIS_DISTANCE_DEADBAND_MM) ? TRUE : FALSE;
    }
    if (s_dist_target_mm_f > 0.0f) {
        return (s_dist_odom_mm >= (s_dist_target_mm_f - CHASSIS_DISTANCE_DEADBAND_MM)) ? TRUE : FALSE;
    }
    return (s_dist_odom_mm <= (s_dist_target_mm_f + CHASSIS_DISTANCE_DEADBAND_MM)) ? TRUE : FALSE;
}

static void chassis_reset_speed_pids(void)
{
    u8_t i;

    for (i = 0U; i < CHASSIS_MOTOR_COUNT; i++) {
        pid_reset(&s_pid[i]);
        s_target_sign[i] = 0;
    }
}

static void chassis_distance_complete(void)
{
    chassis_reset_targets();
    s_dist_cmd_rpm = 0;
    s_dist_target_mm = 0;
    s_dist_target_mm_f = 0.0f;
    s_dist_current_mm = 0;
    s_active = FALSE;
    s_ctrl_mode = CHASSIS_CTRL_IDLE;
    s_dist_odom_valid = FALSE;
    pid_reset(&s_pid_dist);
    chassis_reset_speed_pids();
}

static void chassis_angle_complete(void)
{
    chassis_reset_targets();
    s_angle_turn_rpm = 0;
    s_active = FALSE;
    s_ctrl_mode = CHASSIS_CTRL_IDLE;
    s_angle_odom_valid = FALSE;
    pid_reset(&s_pid_yaw);
    attitude_yaw_hold_set(FALSE);
    chassis_reset_speed_pids();
}

static u8_t chassis_motor_count(void)
{
    const nvs_cfg_t *cfg = nvs_cfg_get();

    if ((cfg != NULL) && (cfg->hw_rev == NVS_HW_REV_CAR_2WD_V1)) {
        return 2U;
    }
    return CHASSIS_MOTOR_COUNT;
}

static f32_t chassis_wrap_yaw_deg(f32_t deg)
{
    f32_t d = deg;

    while (d > 180.0f) {
        d -= 360.0f;
    }
    while (d <= -180.0f) {
        d += 360.0f;
    }
    return d;
}

static s16_t chassis_yaw_deg_to_i16(f32_t deg)
{
    f32_t wrapped = chassis_wrap_yaw_deg(deg);

    if (wrapped >= 0.0f) {
        return (s16_t)(wrapped + 0.5f);
    }
    return (s16_t)(wrapped - 0.5f);
}

static void chassis_odom_enc_snapshot(void)
{
    u8_t i;

    for (i = 0U; i < CHASSIS_MOTOR_COUNT; i++) {
        s_odom_enc_prev[i] = cfg_encoder_count(i);
    }
}

static void chassis_odom_side_deltas(u8_t motor_count, int32_t *d_left, int32_t *d_right)
{
    int32_t c0 = cfg_encoder_count(0);
    int32_t c1 = cfg_encoder_count(1);
    int32_t d0 = cfg_encoder_delta(0, c0 - s_odom_enc_prev[0]);
    int32_t d1 = cfg_encoder_delta(1, c1 - s_odom_enc_prev[1]);

    s_odom_enc_prev[0] = c0;
    s_odom_enc_prev[1] = c1;
    *d_left = d0;
    *d_right = d1;

    if (motor_count < CHASSIS_MOTOR_COUNT) {
        return;
    }

    {
        int32_t c2 = cfg_encoder_count(2);
        int32_t c3 = cfg_encoder_count(3);
        int32_t d2 = cfg_encoder_delta(2, c2 - s_odom_enc_prev[2]);
        int32_t d3 = cfg_encoder_delta(3, c3 - s_odom_enc_prev[3]);

        s_odom_enc_prev[2] = c2;
        s_odom_enc_prev[3] = c3;

        if (d2 != 0) {
            *d_left = (d0 + d2) / 2;
        }
        if (d3 != 0) {
            *d_right = (d1 + d3) / 2;
        }
    }
}

static void chassis_angle_odom_begin_maneuver(void)
{
    /* 每次 SET_ANGLE 从 0 起计本次相对转角，避免多次指令累积与 float 漂移 */
    s_angle_odom_yaw = 0.0f;
    s_angle_odom_valid = TRUE;
    chassis_odom_enc_snapshot();
}

static f32_t chassis_wheel_circ_m(void)
{
    const nvs_kinematics_t *k = cfg_kinematics();
    f32_t wheel_diam_m = (k != NULL) ? k->wheel_diam_m : 0.065f;

    if (wheel_diam_m < 0.01f) {
        wheel_diam_m = 0.065f;
    }
    return 3.14159265f * wheel_diam_m;
}

static f32_t chassis_counts_to_mm(f32_t avg_counts)
{
    f32_t ppr = motion_pulses_per_wheel_rev();
    f32_t circ_m;

    if (ppr < 1.0f) {
        ppr = 1.0f;
    }
    circ_m = chassis_wheel_circ_m();
    return avg_counts / ppr * circ_m * 1000.0f;
}

static void chassis_angle_odom_step(f32_t dt_s)
{
    const nvs_kinematics_t *k;
    f32_t track_m;
    f32_t ppr;
    f32_t wheel_circ_m;
    int32_t d_left;
    int32_t d_right;
    f32_t dyaw_deg;
    u8_t motor_count = chassis_motor_count();

    if ((s_angle_odom_valid == FALSE) || (dt_s <= 0.0f)) {
        return;
    }

    k = cfg_kinematics();
    track_m = (k != NULL) ? k->track_width_m : 0.18f;
    if (track_m < 0.05f) {
        track_m = 0.18f;
    }

    d_left = 0;
    d_right = 0;
    chassis_odom_side_deltas(motor_count, &d_left, &d_right);

    ppr = motion_pulses_per_wheel_rev();
    if (ppr < 1.0f) {
        ppr = 1.0f;
    }
    wheel_circ_m = chassis_wheel_circ_m();
    dyaw_deg = ((f32_t)d_right - (f32_t)d_left) / ppr * wheel_circ_m / track_m * (180.0f / 3.14159265f);
    s_angle_odom_yaw += dyaw_deg;
}

static void chassis_distance_odom_begin_maneuver(void)
{
    s_dist_odom_mm = 0.0f;
    s_dist_odom_valid = TRUE;
    s_dist_current_mm = 0;
    chassis_odom_enc_snapshot();
}

static void chassis_distance_odom_step(f32_t dt_s)
{
    int32_t d_left;
    int32_t d_right;
    f32_t step_mm;
    u8_t motor_count = chassis_motor_count();

    (void)dt_s;

    if (s_dist_odom_valid == FALSE) {
        return;
    }

    d_left = 0;
    d_right = 0;
    chassis_odom_side_deltas(motor_count, &d_left, &d_right);
    step_mm = chassis_counts_to_mm(((f32_t)d_left + (f32_t)d_right) * 0.5f);
    s_dist_odom_mm += step_mm;

    if (s_dist_odom_mm >= 0.0f) {
        s_dist_current_mm = (s32_t)(s_dist_odom_mm + 0.5f);
    } else {
        s_dist_current_mm = (s32_t)(s_dist_odom_mm - 0.5f);
    }
}

static f32_t chassis_default_max_rpm(void)
{
    const nvs_spd_limit_t *lim = cfg_spd_limit();
    f32_t max_rpm = (lim != NULL) ? lim->max_rpm : 300.0f;

    if (max_rpm <= 0.0f) {
        max_rpm = 300.0f;
    }
    return max_rpm;
}

static void chassis_distance_clear_state(void)
{
    s_dist_target_mm = 0;
    s_dist_target_mm_f = 0.0f;
    s_dist_max_rpm = 0;
    s_dist_odom_mm = 0.0f;
    s_dist_odom_valid = FALSE;
    s_dist_current_mm = 0;
    s_dist_cmd_rpm = 0;
    pid_reset(&s_pid_dist);
}

static f32_t chassis_default_max_turn_rpm(void)
{
    return chassis_default_max_rpm() * 0.5f;
}

static void chassis_angle_clear_state(void)
{
    s_angle_target_yaw = 0;
    s_angle_target_yaw_f = 0.0f;
    s_angle_base_rpm = 0;
    s_angle_max_turn_rpm = 0;
    s_angle_current_yaw = 0;
    s_angle_turn_rpm = 0;
    s_angle_yaw_filt = 0.0f;
    s_angle_yaw_filt_valid = FALSE;
    s_angle_odom_valid = FALSE;
    pid_reset(&s_pid_yaw);
    attitude_yaw_hold_set(FALSE);
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

static void chassis_angle_update_lr(f32_t dt_s)
{
    f32_t yaw_deg;
    f32_t gz_dps;
    f32_t yaw_err;
    f32_t turn_rpm;
    f32_t turn_pi;
    f32_t turn_d;
    f32_t max_turn;
    f32_t base_rpm;
    const nvs_pid3_t *g;
    s32_t left;
    s32_t right;

    chassis_angle_odom_step(dt_s);

    if (s_angle_odom_valid != FALSE) {
        yaw_deg = s_angle_odom_yaw;
        if (attitude_get_yaw_control(NULL, &gz_dps) != STATUS_OK) {
            gz_dps = 0.0f;
        }
    } else if (attitude_get_yaw_control(&yaw_deg, &gz_dps) != STATUS_OK) {
        s_angle_current_yaw = 0;
        chassis_apply_lr_rpm(0, 0);
        return;
    }

    if (s_angle_yaw_filt_valid == FALSE) {
        s_angle_yaw_filt = yaw_deg;
        s_angle_yaw_filt_valid = TRUE;
    } else {
        s_angle_yaw_filt += CHASSIS_ANGLE_YAW_LPF_ALPHA * (yaw_deg - s_angle_yaw_filt);
    }

    s_angle_current_yaw = chassis_yaw_deg_to_i16(s_angle_yaw_filt);
    /* 连续域目标 - 当前：相对 Δθ 不受 ±180 wrap 影响 */
    yaw_err = s_angle_target_yaw_f - s_angle_yaw_filt;
    base_rpm = chassis_clamp_rpm((f32_t)s_angle_base_rpm);

    if (fabsf(yaw_err) <= CHASSIS_ANGLE_DEADBAND_DEG) {
        chassis_angle_complete();
        return;
    }

    g = cfg_pid_yaw();
    /*
     * P+I：连续域误差 target - yaw（不 wrap）；D 用 gz 阻尼而非 d(error)/dt，
     * 避免 mag 停转跳变导致 D 项反向猛拉。
     * turn>0 → 右轮更快（left=base-turn, right=base+turn）。
     */
    turn_pi = pid_update(&s_pid_yaw, 0.0f, -yaw_err, dt_s);
    turn_d = 0.0f;
    if (g != NULL) {
        turn_d = -(g->kd * gz_dps * CHASSIS_ANGLE_GZ_DAMP_SCALE);
    }
    turn_rpm = turn_pi + turn_d;

    max_turn = (s_angle_max_turn_rpm > 0)
                   ? (f32_t)s_angle_max_turn_rpm
                   : chassis_default_max_turn_rpm();
    if (turn_rpm > max_turn) {
        turn_rpm = max_turn;
    } else if (turn_rpm < -max_turn) {
        turn_rpm = -max_turn;
    }

    left = (s32_t)(base_rpm - turn_rpm);
    right = (s32_t)(base_rpm + turn_rpm);

    if (turn_rpm >= 0.0f) {
        s_angle_turn_rpm = (s32_t)(turn_rpm + 0.5f);
    } else {
        s_angle_turn_rpm = (s32_t)(turn_rpm - 0.5f);
    }

    chassis_apply_lr_rpm(left, right);
}

static void chassis_distance_update_lr(f32_t dt_s)
{
    f32_t err_mm;
    f32_t cmd_rpm;
    f32_t max_rpm;
    s32_t rpm_cmd;

    chassis_distance_odom_step(dt_s);

    if (chassis_distance_reached() != FALSE) {
        chassis_distance_complete();
        return;
    }

    err_mm = s_dist_target_mm_f - s_dist_odom_mm;
    max_rpm = (s_dist_max_rpm > 0) ? (f32_t)s_dist_max_rpm : chassis_default_max_rpm();

    if (fabsf(err_mm) <= CHASSIS_DISTANCE_DEADBAND_MM) {
        chassis_distance_complete();
        return;
    }

    cmd_rpm = pid_update(&s_pid_dist, s_dist_target_mm_f, s_dist_odom_mm, dt_s);
    if (fabsf(err_mm) <= 25.0f && fabsf(cmd_rpm) < 18.0f) {
        chassis_distance_complete();
        return;
    }
    if (cmd_rpm > max_rpm) {
        cmd_rpm = max_rpm;
    } else if (cmd_rpm < -max_rpm) {
        cmd_rpm = -max_rpm;
    }

    if (cmd_rpm >= 0.0f) {
        rpm_cmd = (s32_t)(cmd_rpm + 0.5f);
    } else {
        rpm_cmd = (s32_t)(cmd_rpm - 0.5f);
    }
    s_dist_cmd_rpm = rpm_cmd;
    chassis_apply_lr_rpm(rpm_cmd, rpm_cmd);
}

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

void chassis_reload_angle_pid_gains(void)
{
    const nvs_pid3_t *g = cfg_pid_yaw();

    if (g == NULL) {
        return;
    }

    pid_set_gains(&s_pid_yaw, g->kp, g->ki, 0.0f);
}

void chassis_reload_distance_pid_gains(void)
{
    const nvs_pid3_t *g = cfg_pid_dist();

    if (g == NULL) {
        return;
    }

    pid_set_gains(&s_pid_dist, g->kp, g->ki, g->kd);
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
    duty_f = ((f32_t)duty_ff * CHASSIS_FF_GAIN) + pid_out;

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

    if (s_chassis_mutex == NULL) {
        s_chassis_mutex = xSemaphoreCreateMutex();
    }

    chassis_reset_targets();
    s_active = FALSE;
    s_ctrl_mode = CHASSIS_CTRL_IDLE;
    chassis_angle_clear_state();
    chassis_distance_clear_state();

    for (i = 0U; i < CHASSIS_MOTOR_COUNT; i++) {
        pid_init(&s_pid[i], 1.0f, 0.0f, 0.0f);
        pid_set_output_limits(&s_pid[i], -CHASSIS_PID_OUT_MAX_PERMILLE, CHASSIS_PID_OUT_MAX_PERMILLE);
        pid_set_integral_limit(&s_pid[i], CHASSIS_PID_INTEGRAL_MAX);
    }

    pid_init(&s_pid_yaw, 2.0f, 0.0f, 0.5f);
    pid_set_output_limits(&s_pid_yaw, -CHASSIS_ANGLE_PID_OUT_MAX_RPM, CHASSIS_ANGLE_PID_OUT_MAX_RPM);
    pid_set_integral_limit(&s_pid_yaw, CHASSIS_ANGLE_PID_INTEGRAL_MAX);

    pid_init(&s_pid_dist, 0.8f, 0.05f, 0.02f);
    pid_set_output_limits(&s_pid_dist, -CHASSIS_DISTANCE_PID_OUT_MAX_RPM, CHASSIS_DISTANCE_PID_OUT_MAX_RPM);
    pid_set_integral_limit(&s_pid_dist, CHASSIS_DISTANCE_PID_INTEGRAL_MAX);

    chassis_reload_pid_gains();
    chassis_reload_angle_pid_gains();
    chassis_reload_distance_pid_gains();
    motion_init();
}

void chassis_stop(void)
{
    u8_t i;
    u8_t motor_count = chassis_motor_count();

    chassis_lock();
    chassis_reset_targets();
    s_active = FALSE;
    s_ctrl_mode = CHASSIS_CTRL_IDLE;
    chassis_angle_clear_state();
    chassis_distance_clear_state();

    for (i = 0U; i < CHASSIS_MOTOR_COUNT; i++) {
        pid_reset(&s_pid[i]);
    }

    if (device_profile_board_wants(DEVICE_BOARD_MASK_MOTOR)) {
        for (i = 1U; i <= motor_count; i++) {
            Motor_SetOutput(i, 0, 0U);
        }
        for (i = motor_count + 1U; i <= CHASSIS_MOTOR_COUNT; i++) {
            Motor_SetOutput(i, 0, 0U);
        }
    }
    chassis_unlock();
}

bool_t chassis_is_active(void)
{
    return s_active;
}

bool_t chassis_angle_is_active(void)
{
    return (s_ctrl_mode == CHASSIS_CTRL_ANGLE) ? TRUE : FALSE;
}

bool_t chassis_distance_is_active(void)
{
    return (s_ctrl_mode == CHASSIS_CTRL_DISTANCE) ? TRUE : FALSE;
}

s16_t chassis_get_angle_target_yaw(void)
{
    return s_angle_target_yaw;
}

s16_t chassis_get_angle_current_yaw(void)
{
    return s_angle_current_yaw;
}

s32_t chassis_get_angle_turn_rpm(void)
{
    return s_angle_turn_rpm;
}

s32_t chassis_get_angle_base_rpm(void)
{
    return s_angle_base_rpm;
}

s32_t chassis_get_distance_target_mm(void)
{
    return s_dist_target_mm;
}

s32_t chassis_get_distance_current_mm(void)
{
    return s_dist_current_mm;
}

s32_t chassis_get_distance_cmd_rpm(void)
{
    return s_dist_cmd_rpm;
}

s32_t chassis_get_distance_max_rpm(void)
{
    if (s_dist_max_rpm > 0) {
        return s_dist_max_rpm;
    }
    return (s32_t)(chassis_default_max_rpm() + 0.5f);
}

void chassis_set_wheel_rpm(u8_t motor_id, s32_t rpm)
{
    if ((motor_id < 1U) || (motor_id > chassis_motor_count())) {
        return;
    }

    s_ctrl_mode = CHASSIS_CTRL_SPEED;
    chassis_angle_clear_state();
    chassis_distance_clear_state();
    s_target_rpm[motor_id - 1U] = chassis_clamp_rpm((f32_t)rpm);
    s_active = TRUE;
}

static void chassis_apply_lr_rpm(s32_t left_rpm, s32_t right_rpm)
{
    u8_t motor_count = chassis_motor_count();

    left_rpm = (s32_t)chassis_clamp_rpm((f32_t)left_rpm);
    right_rpm = (s32_t)chassis_clamp_rpm((f32_t)right_rpm);

    s_target_rpm[0] = (f32_t)left_rpm;
    s_target_rpm[1] = (f32_t)right_rpm;
    if (motor_count >= CHASSIS_MOTOR_COUNT) {
        s_target_rpm[2] = (f32_t)left_rpm;
        s_target_rpm[3] = (f32_t)right_rpm;
    } else {
        s_target_rpm[2] = 0.0f;
        s_target_rpm[3] = 0.0f;
    }
    s_active = TRUE;
}

void chassis_set_lr_rpm(s32_t left_rpm, s32_t right_rpm)
{
    s_ctrl_mode = CHASSIS_CTRL_SPEED;
    chassis_angle_clear_state();
    chassis_distance_clear_state();
    chassis_apply_lr_rpm(left_rpm, right_rpm);
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

    s_ctrl_mode = CHASSIS_CTRL_SPEED;
    chassis_angle_clear_state();
    chassis_distance_clear_state();

    max_rpm = (lim != NULL) ? lim->max_rpm : 300.0f;
    base_rpm = ((f32_t)throttle / (f32_t)throttle_max) * max_rpm;
    turn_rpm = ((f32_t)steer / (f32_t)steer_max) * max_rpm * 0.5f;
    left = (s32_t)(base_rpm - turn_rpm);
    right = (s32_t)(base_rpm + turn_rpm);
    chassis_apply_lr_rpm(left, right);
}

void chassis_set_angle(s16_t target_yaw_deg, s32_t base_rpm, s32_t max_turn_rpm)
{
    s_ctrl_mode = CHASSIS_CTRL_ANGLE;
    chassis_distance_clear_state();
    chassis_reset_speed_pids();
    s_angle_base_rpm = base_rpm;
    s_angle_max_turn_rpm = max_turn_rpm;
    s_angle_turn_rpm = 0;
    s_angle_yaw_filt_valid = FALSE;
    pid_reset(&s_pid_yaw);
    attitude_yaw_hold_set(TRUE);
    chassis_angle_odom_begin_maneuver();

    /* SET_ANGLE：相对当前航向 Δθ；本次机动在连续域 0→Δθ */
    s_angle_target_yaw_f = (f32_t)target_yaw_deg;
    s_angle_target_yaw = chassis_yaw_deg_to_i16(s_angle_target_yaw_f);
    s_active = TRUE;
}

void chassis_set_distance(s32_t target_dist_mm, s32_t max_rpm)
{
    s_ctrl_mode = CHASSIS_CTRL_DISTANCE;
    chassis_angle_clear_state();
    chassis_reset_speed_pids();
    s_dist_max_rpm = max_rpm;
    s_dist_cmd_rpm = 0;
    pid_reset(&s_pid_dist);
    chassis_distance_odom_begin_maneuver();

    s_dist_target_mm_f = (f32_t)target_dist_mm;
    s_dist_target_mm = target_dist_mm;
    s_active = TRUE;
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
    u8_t motor_count = chassis_motor_count();

    if (!device_profile_board_wants(DEVICE_BOARD_MASK_MOTOR | DEVICE_BOARD_MASK_ENCODER)) {
        return;
    }

    chassis_lock();

    motion_update(period_ms);

    if (period_ms == 0U) {
        chassis_unlock();
        return;
    }
    dt_s = (f32_t)period_ms / 1000.0f;

    if (s_ctrl_mode == CHASSIS_CTRL_ANGLE) {
        chassis_angle_update_lr(dt_s);
    } else if (s_ctrl_mode == CHASSIS_CTRL_DISTANCE) {
        chassis_distance_update_lr(dt_s);
    }

    chassis_ramp_targets(dt_s);

    if (!s_active) {
        for (i = 1U; i <= motor_count; i++) {
            Motor_SetOutput(i, 0, 0U);
        }
        chassis_unlock();
        return;
    }

    for (i = 1U; i <= motor_count; i++) {
        f32_t target = s_ramped_rpm[i - 1U];
        f32_t measured = motion_get_logical_rpm(i);

        chassis_motor_output(i, target, measured, dt_s);
    }

    for (i = motor_count + 1U; i <= CHASSIS_MOTOR_COUNT; i++) {
        Motor_SetOutput(i, 0, 0U);
    }
    chassis_unlock();
}
