/**
 * @file    lcd_panel.h
 * @brief   ST7789 1.14" 240×135：主屏双十字 + 菜单/校准绘制
 */

#ifndef BOARD_LCD_PANEL_H
#define BOARD_LCD_PANEL_H

#include "type.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

status_t lcd_panel_init(void);
bool_t lcd_panel_is_ready(void);

/** 主屏：双十字准星骨架（清屏一次） */
void lcd_panel_show_home(void);

/**
 * 刷新主屏动态内容（仅变化区域；无变化则不写屏）。
 * j1/j2 cmd ∈ [-1000,1000]；btn 按下为非 0；bat_percent 0..100（0xFF=未知）。
 * @return TRUE 表示发生了绘制
 */
bool_t lcd_panel_update_home(int16_t j1x, int16_t j1y, int16_t j2x, int16_t j2y,
                             bool_t j1_btn, bool_t j2_btn,
                             uint8_t bat_percent, bool_t link_up,
                             int16_t drive_throttle, int16_t drive_steer);

/** 菜单列表：title + 最多 4 行可见；cursor_vis 为可见区高亮行 0..3 */
void lcd_panel_show_menu(const char *title,
                         const char *const lines[],
                         uint8_t line_count,
                         uint8_t cursor_vis,
                         const char *foot);

/**
 * 仅重绘菜单某一可见行（不碰标题/页脚/其它行）。
 * @param row 可见行 0..3；@param highlighted 是否为当前光标行
 */
void lcd_panel_update_menu_row(uint8_t row, const char *text, bool_t highlighted);

/** 校准向导静态帧（标题/提示/十字准星，切步时调用一次） */
void lcd_panel_show_cal_frame(const char *title, const char *hint);

/**
 * 校准向导动态区（摇杆点 + 坐标/状态行；无变化则不写屏）。
 * @return TRUE 表示发生了绘制
 */
bool_t lcd_panel_update_cal(int16_t j1x, int16_t j1y, int16_t j2x, int16_t j2y,
                            const char *status_line);

#ifdef __cplusplus
}
#endif

#endif /* BOARD_LCD_PANEL_H */
