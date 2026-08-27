/**
 * @file    rc_input.c
 * @brief   双摇杆采样 + 非阻塞按键状态机
 */

#include "rc_input.h"

static bool_t s_js1_prev;
static bool_t s_js2_prev;
static uint16_t s_js2_hold_ms;
static bool_t s_js2_long_fired;

void rc_input_reset(void)
{
    s_js1_prev = FALSE;
    s_js2_prev = FALSE;
    s_js2_hold_ms = 0U;
    s_js2_long_fired = FALSE;
}

bool_t rc_input_tick(uint32_t dt_ms, rc_input_snapshot_t *out)
{
    bool_t js1_down;
    bool_t js2_down;

    if (out == NULL) {
        return FALSE;
    }

    out->valid = FALSE;
    out->js1_edge = FALSE;
    out->js2_edge = FALSE;
    out->js2_down = FALSE;
    out->js2_short = FALSE;
    out->js2_long = FALSE;
    out->js1_js2_combo_edge = FALSE;

    if (!board_joystick_sample(out->js)) {
        return FALSE;
    }

    js1_down = out->js[BOARD_JOYSTICK_1].raw.btn_pressed;
    js2_down = out->js[BOARD_JOYSTICK_2].raw.btn_pressed;

    out->js1_edge = (js1_down && !s_js1_prev) ? TRUE : FALSE;
    out->js2_edge = (js2_down && !s_js2_prev) ? TRUE : FALSE;
    out->js2_down = js2_down;

    if (js1_down && js2_down && (out->js1_edge || out->js2_edge)) {
        out->js1_js2_combo_edge = TRUE;
    }

    if (js2_down) {
        if (!s_js2_long_fired) {
            s_js2_hold_ms = (uint16_t)(s_js2_hold_ms + dt_ms);
            if (s_js2_hold_ms >= RC_INPUT_HOLD_LONG_MS) {
                s_js2_long_fired = TRUE;
                out->js2_long = TRUE;
            }
        }
    } else {
        if (s_js2_prev && (s_js2_hold_ms > 0U) && (s_js2_hold_ms < RC_INPUT_HOLD_LONG_MS)) {
            out->js2_short = TRUE;
        }
        s_js2_hold_ms = 0U;
        s_js2_long_fired = FALSE;
    }

    s_js1_prev = js1_down;
    s_js2_prev = js2_down;
    out->valid = TRUE;
    return TRUE;
}

bool_t rc_input_sticks_centered(const board_joystick_state_t js[BOARD_JOYSTICK_COUNT])
{
    if (js == NULL) {
        return FALSE;
    }
    if ((js[BOARD_JOYSTICK_1].mapped.x_in_deadband == FALSE) ||
        (js[BOARD_JOYSTICK_1].mapped.y_in_deadband == FALSE) ||
        (js[BOARD_JOYSTICK_2].mapped.x_in_deadband == FALSE) ||
        (js[BOARD_JOYSTICK_2].mapped.y_in_deadband == FALSE)) {
        return FALSE;
    }
    return TRUE;
}
