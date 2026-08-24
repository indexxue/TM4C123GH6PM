/**
 * @file    joystick.h
 * @brief   双操纵杆 ADC + GPIO 按键抽象（引脚见 board.h / .syscfg）
 *
 * 轴向约定（实测）：
 *   X：左=0，中≈2048，右=4095
 *   Y：下=0，中≈2048，上=4095
 * 映射输出 cmd ∈ [-BOARD_JOYSTICK_CMD_MAX, +BOARD_JOYSTICK_CMD_MAX]
 */

#ifndef BOARD_JOYSTICK_H
#define BOARD_JOYSTICK_H

#include "type.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BOARD_JOYSTICK_COUNT        2U
#define BOARD_JOYSTICK_AXIS_COUNT   4U
#define BOARD_JOYSTICK_CMD_MAX      1000

/** 12-bit ADC 满量程与回中 */
#define BOARD_JOY_ADC_MIN           0U
#define BOARD_JOY_ADC_CENTER        2048U
#define BOARD_JOY_ADC_MAX           4095U

/** 中心死区与两端预留（机械回中误差 / 极限位不可靠区） */
#define BOARD_JOY_ADC_DEADBAND      120U
#define BOARD_JOY_ADC_EDGE_MARGIN   80U

typedef enum {
    BOARD_JOYSTICK_1 = 0,
    BOARD_JOYSTICK_2 = 1,
} board_joystick_id_t;

/** 轴序：J1X, J1Y, J2X, J2Y（与 joy_cal 一致） */
typedef enum {
    BOARD_JOY_AXIS_J1X = 0,
    BOARD_JOY_AXIS_J1Y = 1,
    BOARD_JOY_AXIS_J2X = 2,
    BOARD_JOY_AXIS_J2Y = 3,
} board_joystick_axis_id_t;

typedef struct {
    uint16_t adc_min;
    uint16_t adc_center;
    uint16_t adc_max;
    uint16_t deadband;
    uint16_t edge_margin;
    int16_t cmd_max;
    bool_t invert;
} board_joystick_axis_cfg_t;

typedef struct {
    uint16_t x_raw;
    uint16_t y_raw;
    bool_t btn_pressed;
} board_joystick_raw_t;

typedef struct {
    int16_t x_cmd;
    int16_t y_cmd;
    bool_t btn_pressed;
    bool_t x_in_deadband;
    bool_t y_in_deadband;
} board_joystick_mapped_t;

typedef struct {
    board_joystick_raw_t raw;
    board_joystick_mapped_t mapped;
} board_joystick_state_t;

/** 摇杆1 驱动量；aux 为摇杆2 预留（功能映射待定） */
typedef struct {
    int16_t throttle;
    int16_t steer;
    board_joystick_mapped_t aux;
} board_joystick_drive_t;

status_t board_joystick_init(void);
bool_t board_joystick_is_ready(void);

void board_joystick_axis_cfg_default(board_joystick_axis_cfg_t *cfg);

/** 设置运行时轴配置（校准/反转/死区）；axis ∈ 0..3 */
void board_joystick_set_axis_cfg(board_joystick_axis_id_t axis,
                                 const board_joystick_axis_cfg_t *cfg);

void board_joystick_get_axis_cfg(board_joystick_axis_id_t axis,
                                 board_joystick_axis_cfg_t *cfg);

int16_t board_joystick_map_axis(const board_joystick_axis_cfg_t *cfg, uint16_t adc_raw,
                                bool_t *in_deadband);

bool_t board_joystick_sample(board_joystick_state_t out[BOARD_JOYSTICK_COUNT]);

/** 采样并填充 js1→throttle/steer，js2→aux */
bool_t board_joystick_sample_drive(board_joystick_drive_t *drive);

#ifdef __cplusplus
}
#endif

#endif /* BOARD_JOYSTICK_H */
