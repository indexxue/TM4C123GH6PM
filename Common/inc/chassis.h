/**
 * @file    chassis.h
 * @brief   底盘速度环 + 航向角环（yaw PID → 左右轮 RPM → 内环速度 PID）
 */

#ifndef CHASSIS_H
#define CHASSIS_H

#include "type.h"

#include <stdint.h>

void chassis_init(void);
/** 从 NVS 重载 pid_speed 增益（PARAM_WRITE / CMD 修改后调用） */
void chassis_reload_pid_gains(void);
/** 从 NVS 重载 pid_yaw 增益 */
void chassis_reload_angle_pid_gains(void);
void chassis_tick(u32_t period_ms);
void chassis_stop(void);

/** @param motor_id 1..4（2wd 仅 M1/M2 有效） */
void chassis_set_wheel_rpm(u8_t motor_id, s32_t rpm);
void chassis_set_lr_rpm(s32_t left_rpm, s32_t right_rpm);
void chassis_set_drive(s32_t throttle, s32_t steer, s32_t throttle_max, s32_t steer_max);

/**
 * 航向角环：相对转角 Δθ + 基准前进 RPM，经 pid_yaw 输出左右差速。
 * @param target_yaw_deg 相对当前航向的转角 Δθ（度，-180~180；非绝对方位）
 * @param base_rpm 基准前进 RPM（可负；0 为原地转向）
 * @param max_turn_rpm 差速上限；<=0 则用 max_rpm * 0.5
 */
void chassis_set_angle(s16_t target_yaw_deg, s32_t base_rpm, s32_t max_turn_rpm);

/** 角度环闭环目标航向（度，绝对，= 下发时当前航向 + Δθ） */
s16_t chassis_get_angle_target_yaw(void);
/** 角度环当前航向（度）；编码器里程计，未就绪时返回 0 */
s16_t chassis_get_angle_current_yaw(void);
/** 角度环 PID 输出的 turn RPM（左减右加） */
s32_t chassis_get_angle_turn_rpm(void);
/** 角度环基准前进 RPM */
s32_t chassis_get_angle_base_rpm(void);

/** 逻辑 RPM（与 cfg_motor_rpm 同坐标系） */
s32_t chassis_get_wheel_rpm(u8_t motor_id);

/** 角度环是否激活 */
bool_t chassis_angle_is_active(void);

bool_t chassis_is_active(void);

#endif /* CHASSIS_H */
