/**
 * @file    rc_mixer.h
 * @brief   输入混控：摇杆 / IMU 倾斜 → throttle + steer
 */

#ifndef RC_MIXER_H
#define RC_MIXER_H

#include "joystick.h"
#include "rc_model.h"
#include "type.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RC_MIXER_TILT_FULL_DEG      30
#define RC_MIXER_TILT_DEADBAND_DEG  3
#define RC_MIXER_ZERO_HOLD_MS       500U

status_t rc_mixer_init(void);

/** @a tilt_wanted 为 TRUE 时尝试自动零点校准 */
void rc_mixer_tick(uint32_t dt_ms, bool_t tilt_wanted);

void rc_mixer_on_model_changed(void);

bool_t rc_mixer_attitude_ready(void);
bool_t rc_mixer_zero_ready(void);

/** 零点校准已累积毫秒（0..RC_MIXER_ZERO_HOLD_MS） */
uint16_t rc_mixer_zero_hold_ms(void);

/** 相对零点后的姿态（度），供水平仪 UI */
void rc_mixer_get_display(int16_t *roll_deg, int16_t *pitch_deg);

/**
 * 相对零点 tilt → 十字准星 cmd（与 DRIVE 一致）；轴向见 rc_mixer_cfg.h。
 */
void rc_mixer_tilt_to_stick(int16_t rel_roll, int16_t rel_pitch,
                            int16_t *x_cmd, int16_t *y_cmd);

void rc_mixer_get_drive(int16_t *throttle, int16_t *steer,
                        rc_model_input_src_t src,
                        const board_joystick_state_t js[BOARD_JOYSTICK_COUNT]);

#ifdef __cplusplus
}
#endif

#endif /* RC_MIXER_H */
