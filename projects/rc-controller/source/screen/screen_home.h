/**
 * @file    screen_home.h
 * @brief   HOME 主屏绘制（双十字 + 顶栏）
 */

#ifndef SCREEN_HOME_H
#define SCREEN_HOME_H

#include "joystick.h"
#include "rc_link.h"
#include "type.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void screen_home_show(void);

/**
 * @param link_state  OFF/CONNECTING/CONNECTED
 * @param tip         覆盖底栏（如 NO LINK / CENTER）
 */
void screen_home_paint(const board_joystick_state_t js[BOARD_JOYSTICK_COUNT],
                       uint32_t dt_ms,
                       rc_link_state_t link_state,
                       const char *tip);

#ifdef __cplusplus
}
#endif

#endif /* SCREEN_HOME_H */
