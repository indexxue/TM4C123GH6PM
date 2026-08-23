/**
 * @file    joystick.h
 * @brief   双操纵杆 ADC + GPIO 按键（引脚见 board.h / .syscfg）
 */

#ifndef BOARD_JOYSTICK_H
#define BOARD_JOYSTICK_H

#include "type.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BOARD_JOYSTICK_COUNT   2U

typedef struct {
    uint16_t x;
    uint16_t y;
    bool_t btn_pressed;
} board_joystick_sample_t;

status_t board_joystick_init(void);
bool_t board_joystick_sample(board_joystick_sample_t out[BOARD_JOYSTICK_COUNT]);

/** ADC 原始值 → 协议 DRIVE 量纲 [-1000, 1000]（中心约 2048） */
int16_t board_joystick_axis_to_cmd(uint16_t adc_raw);

#ifdef __cplusplus
}
#endif

#endif /* BOARD_JOYSTICK_H */
