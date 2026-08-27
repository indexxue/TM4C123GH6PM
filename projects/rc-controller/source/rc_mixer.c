/**
 * @file    rc_mixer.c
 * @brief   MPU6050 6-DOF 倾斜映射 + 静止零点
 */

#include "rc_mixer.h"

#include "attitude.h"
#include "imu.h"
#include "rc_mixer_cfg.h"

#include <stdlib.h>

#define RC_MIXER_ZERO_MAX_TILT_DEG  12
#define RC_MIXER_ZERO_MAX_DELTA_DEG 3

static bool_t s_att_ready;
static bool_t s_zero_valid;
static int16_t s_zero_roll;
static int16_t s_zero_pitch;
static int16_t s_roll;
static int16_t s_pitch;
static int16_t s_prev_roll;
static int16_t s_prev_pitch;
static uint16_t s_hold_ms;
static int16_t s_smooth_steer;
static int16_t s_smooth_throttle;

static void rc_mixer_smooth_reset(void)
{
    s_smooth_steer = 0;
    s_smooth_throttle = 0;
}

static int16_t rc_mixer_smooth_step(int16_t prev, int16_t target)
{
#if (RC_MIXER_TILT_SMOOTH_SHIFT > 0)
    int32_t delta = (int32_t)target - (int32_t)prev;

    return (int16_t)(prev + (delta >> RC_MIXER_TILT_SMOOTH_SHIFT));
#else
    (void)prev;
    return target;
#endif
}

static int16_t rc_mixer_abs16(int16_t v)
{
    return (v < 0) ? (int16_t)(-v) : v;
}

static int16_t rc_mixer_angle_to_cmd(int16_t angle_deg)
{
    int16_t a = angle_deg;
    int32_t cmd;

    if (a > RC_MIXER_TILT_FULL_DEG) {
        a = (int16_t)RC_MIXER_TILT_FULL_DEG;
    } else if (a < (int16_t)(-RC_MIXER_TILT_FULL_DEG)) {
        a = (int16_t)(-RC_MIXER_TILT_FULL_DEG);
    }

    if (rc_mixer_abs16(a) <= RC_MIXER_TILT_DEADBAND_DEG) {
        return 0;
    }

    if (a > 0) {
        a = (int16_t)(a - RC_MIXER_TILT_DEADBAND_DEG);
    } else {
        a = (int16_t)(a + RC_MIXER_TILT_DEADBAND_DEG);
    }

    cmd = ((int32_t)a * 1000) / RC_MIXER_TILT_FULL_DEG;
    if (cmd > 1000) {
        cmd = 1000;
    } else if (cmd < -1000) {
        cmd = -1000;
    }
    return (int16_t)cmd;
}

static int16_t rc_mixer_tilt_apply_sign_roll(int16_t rel_roll)
{
#if RC_MIXER_TILT_INVERT_ROLL
    return (int16_t)(-rel_roll);
#else
    return rel_roll;
#endif
}

static int16_t rc_mixer_tilt_apply_sign_pitch(int16_t rel_pitch)
{
#if RC_MIXER_TILT_INVERT_PITCH
    return (int16_t)(-rel_pitch);
#else
    return rel_pitch;
#endif
}

static bool_t rc_mixer_update_attitude(void)
{
    imu_sample_t sample;
    attitude_euler_t euler;

    if (imu_is_ready() == FALSE) {
        return FALSE;
    }
    if (imu_read_sample(&sample) != STATUS_OK) {
        return FALSE;
    }
    if (attitude_update_from_imu(&sample) != STATUS_OK) {
        return FALSE;
    }
    if (attitude_get_euler(&euler) != STATUS_OK) {
        return FALSE;
    }

    s_roll = euler.roll;
    s_pitch = euler.pitch;
    s_att_ready = TRUE;
    return TRUE;
}

static void rc_mixer_zero_tick(uint32_t dt_ms, bool_t tilt_wanted)
{
    int16_t dr;
    int16_t dp;

    if ((tilt_wanted == FALSE) || (s_att_ready == FALSE)) {
        s_hold_ms = 0U;
        return;
    }
    if (s_zero_valid != FALSE) {
        return;
    }

    dr = (int16_t)(s_roll - s_prev_roll);
    dp = (int16_t)(s_pitch - s_prev_pitch);
    if (dr < 0) {
        dr = (int16_t)(-dr);
    }
    if (dp < 0) {
        dp = (int16_t)(-dp);
    }

    if ((rc_mixer_abs16(s_roll) <= RC_MIXER_ZERO_MAX_TILT_DEG) &&
        (rc_mixer_abs16(s_pitch) <= RC_MIXER_ZERO_MAX_TILT_DEG) &&
        (dr <= RC_MIXER_ZERO_MAX_DELTA_DEG) && (dp <= RC_MIXER_ZERO_MAX_DELTA_DEG)) {
        s_hold_ms = (uint16_t)(s_hold_ms + dt_ms);
    } else {
        s_hold_ms = 0U;
    }

    s_prev_roll = s_roll;
    s_prev_pitch = s_pitch;

    if (s_hold_ms >= RC_MIXER_ZERO_HOLD_MS) {
        s_zero_roll = s_roll;
        s_zero_pitch = s_pitch;
        s_zero_valid = TRUE;
        rc_mixer_smooth_reset();
    }
}

status_t rc_mixer_init(void)
{
    s_att_ready = FALSE;
    s_zero_valid = FALSE;
    s_zero_roll = 0;
    s_zero_pitch = 0;
    s_roll = 0;
    s_pitch = 0;
    s_prev_roll = 0;
    s_prev_pitch = 0;
    s_hold_ms = 0U;
    rc_mixer_smooth_reset();
    return STATUS_OK;
}

void rc_mixer_tick(uint32_t dt_ms, bool_t tilt_wanted)
{
    if (tilt_wanted == FALSE) {
        return;
    }
    if (rc_mixer_update_attitude() == FALSE) {
        return;
    }
    rc_mixer_zero_tick(dt_ms, tilt_wanted);
}

void rc_mixer_on_model_changed(void)
{
    s_zero_valid = FALSE;
    s_hold_ms = 0U;
    s_prev_roll = s_roll;
    s_prev_pitch = s_pitch;
    rc_mixer_smooth_reset();
}

bool_t rc_mixer_attitude_ready(void)
{
    return s_att_ready;
}

bool_t rc_mixer_zero_ready(void)
{
    return s_zero_valid;
}

uint16_t rc_mixer_zero_hold_ms(void)
{
    return s_hold_ms;
}

void rc_mixer_get_display(int16_t *roll_deg, int16_t *pitch_deg)
{
    if (roll_deg != NULL) {
        *roll_deg = (int16_t)(s_roll - s_zero_roll);
    }
    if (pitch_deg != NULL) {
        *pitch_deg = (int16_t)(s_pitch - s_zero_pitch);
    }
}

void rc_mixer_tilt_to_stick(int16_t rel_roll, int16_t rel_pitch,
                            int16_t *x_cmd, int16_t *y_cmd)
{
    int16_t roll = rc_mixer_tilt_apply_sign_roll(rel_roll);
    int16_t pitch = rc_mixer_tilt_apply_sign_pitch(rel_pitch);
    int16_t steer_src;
    int16_t throttle_src;

#if RC_MIXER_TILT_SWAP_AXES
    steer_src = roll;
    throttle_src = pitch;
#else
    steer_src = pitch;
    throttle_src = roll;
#endif

    {
        int16_t raw_x = rc_mixer_angle_to_cmd(steer_src);
        int16_t raw_y = rc_mixer_angle_to_cmd(throttle_src);

        s_smooth_steer = rc_mixer_smooth_step(s_smooth_steer, raw_x);
        s_smooth_throttle = rc_mixer_smooth_step(s_smooth_throttle, raw_y);
        if (x_cmd != NULL) {
            *x_cmd = s_smooth_steer;
        }
        if (y_cmd != NULL) {
            *y_cmd = s_smooth_throttle;
        }
    }
}

void rc_mixer_get_drive(int16_t *throttle, int16_t *steer,
                        rc_model_input_src_t src,
                        const board_joystick_state_t js[BOARD_JOYSTICK_COUNT])
{
    int16_t t = 0;
    int16_t s = 0;

    if (src == RC_MODEL_INPUT_IMU_TILT) {
        if ((s_att_ready != FALSE) && (s_zero_valid != FALSE)) {
            int16_t roll = (int16_t)(s_roll - s_zero_roll);
            int16_t pitch = (int16_t)(s_pitch - s_zero_pitch);

            rc_mixer_tilt_to_stick(roll, pitch, &s, &t);
        }
    } else if (js != NULL) {
        t = js[BOARD_JOYSTICK_1].mapped.y_cmd;
        s = js[BOARD_JOYSTICK_1].mapped.x_cmd;
    }

    if (throttle != NULL) {
        *throttle = t;
    }
    if (steer != NULL) {
        *steer = s;
    }
}
