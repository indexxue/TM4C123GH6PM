/**
 * @file    chassis.h
 * @brief   底盘速度环 + 航向角环 + 距离环 + 循迹外环
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
/** 从 NVS 重载 pid_dist 增益 */
void chassis_reload_distance_pid_gains(void);
/** 从 NVS 重载 pid_line 增益 */
void chassis_reload_line_pid_gains(void);
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

/**
 * 距离环：相对位移 Δs + 最大前进 RPM，经 pid_dist 输出左右同速。
 * @param target_dist_mm 相对下发时刻的位移（mm；正=前进，负=后退）
 * @param max_rpm 速度上限；<=0 则用 spd_limit.max_rpm
 */
void chassis_set_distance(s32_t target_dist_mm, s32_t max_rpm);

/**
 * 循迹外环：ADC 偏差 → pid_line → 左右差速，内环仍为速度 PID。
 * @param base_rpm 基准前进 RPM；<=0 使用 NVS line_base_rpm
 */
void chassis_set_line_follow(s32_t base_rpm);

/** 角度环闭环目标航向（度，绝对，= 下发时当前航向 + Δθ） */
s16_t chassis_get_angle_target_yaw(void);
/** 角度环当前航向（度）；编码器里程计，未就绪时返回 0 */
s16_t chassis_get_angle_current_yaw(void);
/** 角度环 PID 输出的 turn RPM（左减右加） */
s32_t chassis_get_angle_turn_rpm(void);
/** 角度环基准前进 RPM */
s32_t chassis_get_angle_base_rpm(void);

/** 距离环目标位移（mm，相对本次机动起点） */
s32_t chassis_get_distance_target_mm(void);
/** 距离环当前位移（mm，编码器积分） */
s32_t chassis_get_distance_current_mm(void);
/** 距离环 PID 输出 RPM（左右同速） */
s32_t chassis_get_distance_cmd_rpm(void);
/** 距离环速度上限 RPM */
s32_t chassis_get_distance_max_rpm(void);

/** 循迹横向偏差（加权平均，约 -5..+5） */
f32_t chassis_get_line_error(void);
/** 循迹检测位掩码 */
uint8_t chassis_get_line_detect_mask(void);

/** 逻辑 RPM（与 cfg_motor_rpm 同坐标系） */
s32_t chassis_get_wheel_rpm(u8_t motor_id);

/** 4 路编码器原始计数（用于遥测诊断 M3/M4） */
void chassis_get_encoder_counts(s32_t out[4]);

/** 角度环是否激活 */
bool_t chassis_angle_is_active(void);
/** 距离环是否激活 */
bool_t chassis_distance_is_active(void);
/** 循迹环是否激活 */
bool_t chassis_line_follow_is_active(void);

bool_t chassis_is_active(void);

#endif /* CHASSIS_H */
