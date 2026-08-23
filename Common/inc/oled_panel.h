/**
 * @file    oled_panel.h
 * @brief   SSD1306 OLED 公共模块（I2C0 @ 0x3C，128×64）
 *
 * 与 IMU/磁力计共用软 I2C0：仅在非控制关键路径写屏，写完后恢复总线空闲。
 */

#ifndef OLED_PANEL_H
#define OLED_PANEL_H

#include "type.h"

#include <stdint.h>

/** SSD1306 7-bit 地址（与 IMU/磁力计共用 I2C0） */
#define OLED_PANEL_I2C_ADDR_DEFAULT 0x3Cu
#define OLED_PANEL_WIDTH            128u
#define OLED_PANEL_HEIGHT           64u

status_t oled_panel_init(void);
bool_t oled_panel_is_ready(void);

/** 循迹结束时显示总时长（秒.毫秒）；勿在控制环内频繁调用 */
void oled_panel_show_lf_result(uint32_t elapsed_ms);

#endif /* OLED_PANEL_H */
