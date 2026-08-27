/**
 * @file    screen_target.h
 * @brief   目标设备选择 overlay
 */

#ifndef SCREEN_TARGET_H
#define SCREEN_TARGET_H

#include "type.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

bool_t screen_target_is_active(void);
void screen_target_open(void);
void screen_target_close(void);
void screen_target_paint(void);

/** JS2 左右拨动切换槽位（带阈值防抖） */
void screen_target_nav_js2(int16_t js2_x_cmd);

/** JS1 确认当前槽位为 active */
status_t screen_target_confirm(void);

#ifdef __cplusplus
}
#endif

#endif /* SCREEN_TARGET_H */
