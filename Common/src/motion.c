/**
 * @file    motion.c
 * @brief   编码器差分测速
 */

#include "motion.h"

#include "board.h"
#include "cfg.h"

#define MOTION_RPM_FILTER_ALPHA 0.30f
#define MOTION_MIN_DUTY_PERMILLE 120U

static f32_t s_rpm[MOTION_WHEEL_COUNT];
static int32_t s_prev_count[MOTION_WHEEL_COUNT];
static bool_t s_prev_valid;


f32_t motion_pulses_per_wheel_rev(void)
{
    const nvs_kinematics_t *k = cfg_kinematics();
    f32_t ppr;

    if ((k == NULL) || (k->encoder_cpr == 0U) || (k->gear_ratio <= 0.0f)) {
        return 1.0f;
    }

    ppr = (f32_t)k->encoder_cpr * k->gear_ratio * 4.0f;
    if (ppr < 1.0f) {
        return 1.0f;
    }
    return ppr;
}

void motion_init(void)
{
    u8_t i;

    for (i = 0U; i < MOTION_WHEEL_COUNT; i++) {
        s_rpm[i] = 0.0f;
        s_prev_count[i] = cfg_encoder_count(i);
    }
    s_prev_valid = FALSE;
}

void motion_update(u32_t period_ms)
{
    f32_t ppr;
    f32_t dt_min;
    u8_t i;

    if (period_ms == 0U) {
        return;
    }

    ppr = motion_pulses_per_wheel_rev();
    dt_min = (f32_t)period_ms / 60000.0f;

    for (i = 0U; i < MOTION_WHEEL_COUNT; i++) {
        int32_t now = cfg_encoder_count(i);
        int32_t delta = cfg_encoder_delta(i, now - s_prev_count[i]);
        f32_t rpm_raw;
        f32_t rpm;

        s_prev_count[i] = now;
        if (s_prev_valid == FALSE) {
            s_rpm[i] = 0.0f;
            continue;
        }

        rpm_raw = ((f32_t)delta / ppr) / dt_min;
        rpm = rpm_raw;

        s_rpm[i] += MOTION_RPM_FILTER_ALPHA * (rpm - s_rpm[i]);
    }

    s_prev_valid = TRUE;
}

f32_t motion_get_rpm(u8_t wheel_index)
{
    if (wheel_index >= MOTION_WHEEL_COUNT) {
        return 0.0f;
    }
    return s_rpm[wheel_index];
}

f32_t motion_get_logical_rpm(u8_t motor_id)
{
    if ((motor_id < 1U) || (motor_id > MOTION_WHEEL_COUNT)) {
        return 0.0f;
    }
    return s_rpm[motor_id - 1U];
}

u16_t motion_rpm_to_duty_permille(f32_t abs_rpm)
{
    const nvs_spd_limit_t *lim = cfg_spd_limit();
    f32_t max_rpm;
    f32_t duty_f;

    if (abs_rpm < 0.0f) {
        abs_rpm = -abs_rpm;
    }
    if (abs_rpm <= 0.0f) {
        return 0U;
    }

    max_rpm = (lim != NULL) ? lim->max_rpm : 300.0f;
    if (max_rpm <= 0.0f) {
        max_rpm = 300.0f;
    }
    if (abs_rpm > max_rpm) {
        abs_rpm = max_rpm;
    }

    duty_f = (abs_rpm * 1000.0f) / max_rpm;
    if (duty_f > 1000.0f) {
        duty_f = 1000.0f;
    }
    if (duty_f < (f32_t)MOTION_MIN_DUTY_PERMILLE) {
        duty_f = (f32_t)MOTION_MIN_DUTY_PERMILLE;
    }
    return (u16_t)duty_f;
}
