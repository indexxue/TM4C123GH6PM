/**
 * @file    rc_lcd_cfg.h
 * @brief   rc-controller 屏向/色序/主题色（编译期宏，改完重编）
 *
 * 当前屏：ST7789 1.14" 240×135，像素格式 RGB565。
 * 驱动默认 MADCTL.BGR=1（多数模块为 BGR 屏），软件仍用 RGB565 宏写颜色。
 */

#ifndef RC_LCD_CFG_H
#define RC_LCD_CFG_H

#include "lcd.h"
#include "st7789.h"

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------- */
/* 方向                                                                        */
/* -------------------------------------------------------------------------- */

/**
 * 基座方向（未翻转时）：
 *   ST7789_ROT_LANDSCAPE     = 2  横屏（当前默认）
 *   ST7789_ROT_LANDSCAPE_270 = 3  横屏另一向（上下对调常见解）
 *   ST7789_ROT_PORTRAIT_0/180 = 0/1 竖屏
 */
#ifndef RC_LCD_ROTATION_BASE
#define RC_LCD_ROTATION_BASE   ST7789_ROT_LANDSCAPE
#endif

/**
 * 上下翻转：1 → 在横屏基座上切到另一横屏方向（2↔3）。
 * 本板实测画面上下颠倒，默认打开。
 */
#ifndef RC_LCD_FLIP_UD
#define RC_LCD_FLIP_UD         1
#endif

#if (RC_LCD_FLIP_UD)
#if (RC_LCD_ROTATION_BASE == ST7789_ROT_LANDSCAPE)
#define RC_LCD_ROTATION        ST7789_ROT_LANDSCAPE_270
#elif (RC_LCD_ROTATION_BASE == ST7789_ROT_LANDSCAPE_270)
#define RC_LCD_ROTATION        ST7789_ROT_LANDSCAPE
#elif (RC_LCD_ROTATION_BASE == ST7789_ROT_PORTRAIT_0)
#define RC_LCD_ROTATION        ST7789_ROT_PORTRAIT_180
#else
#define RC_LCD_ROTATION        ST7789_ROT_PORTRAIT_0
#endif
#else
#define RC_LCD_ROTATION        RC_LCD_ROTATION_BASE
#endif

/* -------------------------------------------------------------------------- */
/* 色序（MADCTL BGR 位）                                                       */
/* -------------------------------------------------------------------------- */

/**
 * 1 = BGR；0 = RGB。
 * 本板在 BGR=1 时深蓝背景会呈棕黄（R/B 对调），故默认 RGB。
 */
#ifndef RC_LCD_BGR
#define RC_LCD_BGR             0
#endif

/* -------------------------------------------------------------------------- */
/* 主题色（RGB565）                                                            */
/* -------------------------------------------------------------------------- */

/** 由 8bit R/G/B 组装 RGB565 */
#define RC_LCD_RGB565(r, g, b)                                                 \
    ((uint16_t)(((((uint16_t)(r) & 0xF8U) << 8) |                              \
                 (((uint16_t)(g) & 0xFCU) << 3) |                              \
                 (((uint16_t)(b) & 0xF8U) >> 3))))

/** 主屏背景：偏深蓝（非棕黄） */
#ifndef RC_LCD_COLOR_BG
#define RC_LCD_COLOR_BG        RC_LCD_RGB565(8, 24, 72)
#endif
#ifndef RC_LCD_COLOR_FG
#define RC_LCD_COLOR_FG        LCD_COLOR_WHITE
#endif
#ifndef RC_LCD_COLOR_ACCENT
#define RC_LCD_COLOR_ACCENT    LCD_COLOR_CYAN
#endif
#ifndef RC_LCD_COLOR_WARN
#define RC_LCD_COLOR_WARN      LCD_COLOR_YELLOW
#endif
#ifndef RC_LCD_COLOR_OK
#define RC_LCD_COLOR_OK        LCD_COLOR_GREEN
#endif
#ifndef RC_LCD_COLOR_MUTED
#define RC_LCD_COLOR_MUTED     LCD_COLOR_GRAY
#endif
#ifndef RC_LCD_COLOR_CROSS
#define RC_LCD_COLOR_CROSS     LCD_COLOR_LGRAY
#endif
#ifndef RC_LCD_COLOR_DOT
#define RC_LCD_COLOR_DOT       LCD_COLOR_CYAN
#endif
#ifndef RC_LCD_COLOR_DOT_BTN
#define RC_LCD_COLOR_DOT_BTN   LCD_COLOR_RED
#endif

#ifdef __cplusplus
}
#endif

#endif /* RC_LCD_CFG_H */
