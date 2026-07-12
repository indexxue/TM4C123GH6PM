/**
 * @file    motion.h
 * @brief   编码器测速、RPM↔PWM 标定
 */

#ifndef MOTION_H
#define MOTION_H

#include "type.h"

#include <stdint.h>

#define MOTION_WHEEL_COUNT 4U
/** 非零目标最低前馈占空比（‰），与 board MOTOR_MIN_DUTY 一致 */
#define MOTION_MIN_DUTY_PERMILLE 120U

void motion_init(void);
void motion_update(u32_t period_ms);

/** @param wheel_index 0..MOTION_WHEEL_COUNT-1，对应 M1..M4 编码器 */
f32_t motion_get_rpm(u8_t wheel_index);

/** 逻辑 RPM（用户坐标系；极性由 encoder_dir_mask + encoder_polarity.h 修正） */
f32_t motion_get_logical_rpm(u8_t motor_id);

/** 绝对 RPM → PWM 千分比 [0,1000]（开环前馈） */
u16_t motion_rpm_to_duty_permille(f32_t abs_rpm);

f32_t motion_pulses_per_wheel_rev(void);

#endif /* MOTION_H */
