/**
 * @file    encoder_polarity.h
 * @brief   编码器计数正/负与逻辑前进(+RPM)的对齐宏
 *
 * ENCODER_POL_Mx_FWD_COUNT_POS：
 *   1 — 逻辑前进时编码器 raw delta 为正（与 SET_SPEED +RPM 同向）；
 *   0 — 逻辑前进时 raw delta 为负，对应 flip_mask 位应置 1。
 *
 * 板级覆盖：在 projects/<car>/board/inc/encoder_polarity_board.h 中重定义。
 * NVS encoder_dir_mask 出厂默认 = encoder_polarity_default_mask()，运行时可 PARAM 修改。
 */

#ifndef ENCODER_POLARITY_H
#define ENCODER_POLARITY_H

#include "type.h"

#include <stdint.h>

#if defined(__has_include)
#if __has_include("encoder_polarity_board.h")
#include "encoder_polarity_board.h"
#endif
#endif

#ifndef ENCODER_POL_M1_FWD_COUNT_POS
#define ENCODER_POL_M1_FWD_COUNT_POS  1
#endif
#ifndef ENCODER_POL_M2_FWD_COUNT_POS
#define ENCODER_POL_M2_FWD_COUNT_POS  1
#endif
#ifndef ENCODER_POL_M3_FWD_COUNT_POS
#define ENCODER_POL_M3_FWD_COUNT_POS  1
#endif
#ifndef ENCODER_POL_M4_FWD_COUNT_POS
#define ENCODER_POL_M4_FWD_COUNT_POS  1
#endif

#ifndef MOTOR_POL_M1_CMD_INVERT
#define MOTOR_POL_M1_CMD_INVERT       0
#endif
#ifndef MOTOR_POL_M2_CMD_INVERT
#define MOTOR_POL_M2_CMD_INVERT       0
#endif
#ifndef MOTOR_POL_M3_CMD_INVERT
#define MOTOR_POL_M3_CMD_INVERT       0
#endif
#ifndef MOTOR_POL_M4_CMD_INVERT
#define MOTOR_POL_M4_CMD_INVERT       0
#endif

/** wheel_index 0..3 → 逻辑前进时 raw 计数是否为正：1/0 */
#define ENCODER_POL_FWD_COUNT_POS(w)                                         \
    ((w) == 0U   ? ENCODER_POL_M1_FWD_COUNT_POS                               \
     : (w) == 1U ? ENCODER_POL_M2_FWD_COUNT_POS                               \
     : (w) == 2U ? ENCODER_POL_M3_FWD_COUNT_POS                               \
                 : ENCODER_POL_M4_FWD_COUNT_POS)

/** 编译期默认 flip_mask（bit=1 表示该轮 delta 取反后才是逻辑坐标） */
static inline u32_t encoder_polarity_default_mask(void)
{
    u32_t mask = 0U;

    if (ENCODER_POL_M1_FWD_COUNT_POS == 0) {
        mask |= 1U << 0;
    }
    if (ENCODER_POL_M2_FWD_COUNT_POS == 0) {
        mask |= 1U << 1;
    }
    if (ENCODER_POL_M3_FWD_COUNT_POS == 0) {
        mask |= 1U << 2;
    }
    if (ENCODER_POL_M4_FWD_COUNT_POS == 0) {
        mask |= 1U << 3;
    }
    return mask;
}

/** 编译期默认 motor_dir_mask（bit=1 表示 cfg_motor_rpm 对逻辑 RPM 取反） */
static inline u32_t motor_polarity_default_mask(void)
{
    u32_t mask = 0U;

    if (MOTOR_POL_M1_CMD_INVERT != 0) {
        mask |= 1U << 0;
    }
    if (MOTOR_POL_M2_CMD_INVERT != 0) {
        mask |= 1U << 1;
    }
    if (MOTOR_POL_M3_CMD_INVERT != 0) {
        mask |= 1U << 2;
    }
    if (MOTOR_POL_M4_CMD_INVERT != 0) {
        mask |= 1U << 3;
    }
    return mask;
}

/** raw delta → 逻辑坐标 delta（flip_mask 通常来自 NVS encoder_dir_mask） */
static inline int32_t encoder_polarity_to_logical_delta(u8_t wheel_index, int32_t raw_delta,
                                                        u32_t flip_mask)
{
    u32_t bit;

    if (wheel_index >= 4U) {
        return raw_delta;
    }

    bit = 1U << wheel_index;
    if ((flip_mask & bit) != 0U) {
        return -raw_delta;
    }
    return raw_delta;
}

/** 目标与反馈异号且均在转动：PID 若按有符号误差会把占空比压死 */
static inline bool_t encoder_polarity_rpm_sign_mismatch(f32_t target_rpm, f32_t measured_rpm,
                                                        f32_t min_rpm)
{
    if ((target_rpm * measured_rpm) >= 0.0f) {
        return FALSE;
    }
    if ((target_rpm < 0.0f) ? (target_rpm > -min_rpm) : (target_rpm < min_rpm)) {
        return FALSE;
    }
    if ((measured_rpm < 0.0f) ? (measured_rpm > -min_rpm) : (measured_rpm < min_rpm)) {
        return FALSE;
    }
    return TRUE;
}

#endif /* ENCODER_POLARITY_H */
