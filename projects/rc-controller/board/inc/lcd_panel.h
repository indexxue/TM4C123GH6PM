/**
 * @file    lcd_panel.h
 * @brief   ST7789 1.14" 240×135 横屏显示
 */

#ifndef BOARD_LCD_PANEL_H
#define BOARD_LCD_PANEL_H

#include "type.h"

#ifdef __cplusplus
extern "C" {
#endif

status_t lcd_panel_init(void);
bool_t lcd_panel_is_ready(void);
void lcd_panel_show_boot(void);
void lcd_panel_update_telemetry(int16_t throttle, int16_t steer, uint32_t bat_mv, bool_t link_up);

#ifdef __cplusplus
}
#endif

#endif /* BOARD_LCD_PANEL_H */
