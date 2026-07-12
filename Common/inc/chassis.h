/**
 * @file    chassis.h
 * @brief   底盘速度环：四轮 PID + 运动学设定
 */

#ifndef CHASSIS_H
#define CHASSIS_H

#include "type.h"

#include <stdint.h>

void chassis_init(void);
/** 从 NVS 重载 pid_speed 增益（PARAM_WRITE / CMD 修改后调用） */
void chassis_reload_pid_gains(void);
void chassis_tick(u32_t period_ms);
void chassis_stop(void);

/** @param motor_id 1..4 */
void chassis_set_wheel_rpm(u8_t motor_id, s32_t rpm);
void chassis_set_lr_rpm(s32_t left_rpm, s32_t right_rpm);
void chassis_set_drive(s32_t throttle, s32_t steer, s32_t throttle_max, s32_t steer_max);

/** 逻辑 RPM（与 cfg_motor_rpm 同坐标系） */
s32_t chassis_get_wheel_rpm(u8_t motor_id);

bool_t chassis_is_active(void);

#endif /* CHASSIS_H */
