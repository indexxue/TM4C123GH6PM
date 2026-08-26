/**
 * @file    rc_ui.h
 * @brief   遥控器 UI：HOME(IDLE) / DRIVE / SUBSCRIBE / MENU / CAL
 */

#ifndef RC_UI_H
#define RC_UI_H

#include "type.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    RC_UI_MODE_HOME = 0,     /**< 空闲：主屏，不发 DRIVE */
    RC_UI_MODE_DRIVE,        /**< 遥控会话：遥测页 + JS1→DRIVE */
    RC_UI_MODE_SUBSCRIBE,    /**< 订阅勾选（会话保持，mute） */
    RC_UI_MODE_MENU,
    RC_UI_MODE_CAL,
} rc_ui_mode_t;

status_t rc_ui_init(void);

/** 20ms 周期调用：采样、导航、绘制、DRIVE 禁发判定 */
void rc_ui_tick(uint32_t dt_ms);

rc_ui_mode_t rc_ui_mode(void);

/** 仅 DRIVE 且链路 UP 时为 FALSE；SUBSCRIBE/HOME/MENU/CAL 或闪断禁发 */
bool_t rc_ui_drive_muted(void);

int16_t rc_ui_last_throttle(void);
int16_t rc_ui_last_steer(void);

#ifdef __cplusplus
}
#endif

#endif /* RC_UI_H */
