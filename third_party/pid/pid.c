/**
 * @file    pid.c
 * @brief   位置式 PID（输出限幅 + 积分反算抗饱和）
 */

#include "pid.h"

#include <stddef.h>

static float pid_clampf(float v, float lo, float hi)
{
    if (v < lo) {
        return lo;
    }
    if (v > hi) {
        return hi;
    }
    return v;
}

void pid_init(pid_t *pid, float kp, float ki, float kd)
{
    if (pid == NULL) {
        return;
    }

    pid->kp = kp;
    pid->ki = ki;
    pid->kd = kd;
    pid->out_min = -1000.0f;
    pid->out_max = 1000.0f;
    pid->integral_max = 1000.0f;
    pid->integral = 0.0f;
    pid->prev_error = 0.0f;
    pid->prev_valid = false;
}

void pid_reset(pid_t *pid)
{
    if (pid == NULL) {
        return;
    }

    pid->integral = 0.0f;
    pid->prev_error = 0.0f;
    pid->prev_valid = false;
}

void pid_set_gains(pid_t *pid, float kp, float ki, float kd)
{
    if (pid == NULL) {
        return;
    }

    pid->kp = kp;
    pid->ki = ki;
    pid->kd = kd;
}

void pid_set_output_limits(pid_t *pid, float out_min, float out_max)
{
    if (pid == NULL) {
        return;
    }

    if (out_min > out_max) {
        float tmp = out_min;
        out_min = out_max;
        out_max = tmp;
    }

    pid->out_min = out_min;
    pid->out_max = out_max;
}

void pid_set_integral_limit(pid_t *pid, float abs_max)
{
    if (pid == NULL) {
        return;
    }

    if (abs_max < 0.0f) {
        abs_max = -abs_max;
    }
    pid->integral_max = abs_max;
}

float pid_update(pid_t *pid, float setpoint, float measurement, float dt_s)
{
    float error;
    float p_term;
    float d_term;
    float out_unsat;
    float out;

    if ((pid == NULL) || (dt_s <= 0.0f)) {
        return 0.0f;
    }

    error = setpoint - measurement;
    p_term = pid->kp * error;

    pid->integral += pid->ki * error * dt_s;
    pid->integral = pid_clampf(pid->integral, -pid->integral_max, pid->integral_max);

    if (pid->prev_valid) {
        d_term = pid->kd * (error - pid->prev_error) / dt_s;
    } else {
        d_term = 0.0f;
        pid->prev_valid = true;
    }
    pid->prev_error = error;

    out_unsat = p_term + pid->integral + d_term;
    out = pid_clampf(out_unsat, pid->out_min, pid->out_max);

    /* 积分反算：输出饱和时回退积分，减轻 windup */
    if ((pid->ki != 0.0f) && (out != out_unsat)) {
        pid->integral += (out - out_unsat) / pid->ki;
        pid->integral = pid_clampf(pid->integral, -pid->integral_max, pid->integral_max);
    }

    return out;
}
