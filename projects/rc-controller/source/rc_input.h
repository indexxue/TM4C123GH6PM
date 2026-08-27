/**
 * @file    rc_input.h
 * @brief   摇杆采样与按键边沿（含 JS1+JS2 组合）
 */

#ifndef RC_INPUT_H
#define RC_INPUT_H

#include "joystick.h"
#include "type.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RC_INPUT_HOLD_LONG_MS    1500U

typedef struct {
    board_joystick_state_t js[BOARD_JOYSTICK_COUNT];
    bool_t js1_edge;
    bool_t js2_edge;
    bool_t js2_down;
    /** JS2 短按松手（按住 < RC_INPUT_HOLD_LONG_MS） */
    bool_t js2_short;
    /** 本帧刚达到长按阈值 */
    bool_t js2_long;
    /** 两键同时按下且至少一键上升沿 */
    bool_t js1_js2_combo_edge;
    bool_t valid;
} rc_input_snapshot_t;

void rc_input_reset(void);

/** 采样摇杆并更新按键边沿；@a dt_ms 用于 JS2 长按/短按判定 */
bool_t rc_input_tick(uint32_t dt_ms, rc_input_snapshot_t *out);

bool_t rc_input_sticks_centered(const board_joystick_state_t js[BOARD_JOYSTICK_COUNT]);

#ifdef __cplusplus
}
#endif

#endif /* RC_INPUT_H */
