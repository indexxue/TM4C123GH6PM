/**
 * @file    pid.h
 * @brief   位置式 PID，带积分限幅与输出饱和抗 windup（MIT 风格第三方库）
 */

#ifndef PID_H
#define PID_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float kp;
    float ki;
    float kd;
    float out_min;
    float out_max;
    float integral_max;
    float integral;
    float prev_error;
    bool prev_valid;
} pid_t;

void pid_init(pid_t *pid, float kp, float ki, float kd);
void pid_reset(pid_t *pid);
void pid_set_gains(pid_t *pid, float kp, float ki, float kd);
void pid_set_output_limits(pid_t *pid, float out_min, float out_max);
void pid_set_integral_limit(pid_t *pid, float abs_max);

/** @param dt_s 控制周期（秒），须 > 0 */
float pid_update(pid_t *pid, float setpoint, float measurement, float dt_s);

#ifdef __cplusplus
}
#endif

#endif /* PID_H */
