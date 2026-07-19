/**
 * @file    line_follow.h
 * @brief   循迹外环：ADC 二值化 → 横向偏差 → PID 左右轮速
 */

#ifndef MODULE_LINE_FOLLOW_H
#define MODULE_LINE_FOLLOW_H

#include "type.h"

#include <stddef.h>
#include <stdint.h>

typedef enum {
    LINE_FOLLOW_STATE_IDLE = 0,
    LINE_FOLLOW_STATE_TRACK,
    LINE_FOLLOW_STATE_LOST,
} line_follow_state_t;

void line_follow_init(void);
void line_follow_reset(void);
void line_follow_reload_pid(void);
/** 覆盖本次循迹基准 RPM（<=0 则回退 NVS line_base_rpm） */
void line_follow_set_base_rpm(f32_t base_rpm);

/** 采样 ADC，更新偏差/状态，输出左右目标 RPM。
 *  全白时沿上次偏差方向短时搜索；超时由底盘层停车。 */
void line_follow_update(f32_t dt_s, f32_t *left_rpm_out, f32_t *right_rpm_out);

f32_t line_follow_get_error(void);
line_follow_state_t line_follow_get_state(void);
uint8_t line_follow_get_detect_mask(void);
u16_t line_follow_get_lost_frames(void);
f32_t line_follow_get_left_rpm(void);
f32_t line_follow_get_right_rpm(void);
f32_t line_follow_get_turn_rpm(void);

/** 供遥测：按 NVS 阈值+极性生成检测位掩码 */
uint8_t line_follow_mask_from_adc(const uint16_t *adc, size_t count);

#endif /* MODULE_LINE_FOLLOW_H */
