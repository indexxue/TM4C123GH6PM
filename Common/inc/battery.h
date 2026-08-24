/**
 * @file battery.h
 * @brief 电池电压采样与 SOC 百分比（ADC1 AIN0 @ PE3）
 *
 * 分压比与 SOC 量程由各产品 board.h 覆盖（BOARD_BATTERY_*）。
 * 默认：小车 1M+200k / 3S；遥控器 100K/100K / 1S。
 * mV→百分比换算仅在本模块内完成，应用层只读结果。
 */

#ifndef BATTERY_H
#define BATTERY_H

#include "type.h"

#include <stdint.h>

#define BATTERY_LEVEL_NUM (4U)

/** 尚未采到有效样本时的百分比哨兵 */
#define BATTERY_PERCENT_UNKNOWN (0xFFU)

typedef struct {
    uint16_t min_mv;
    uint16_t max_mv;
    uint16_t current_mv;
} battery_voltage_t;

typedef struct {
    uint8_t percent; /* 0..100 */
    uint8_t level;
    bool_t charging;
} battery_info_t;

void battery_init(void);

/** 读电池电压（mV）；失败返回 0 */
uint32_t battery_voltage_read_mv(battery_voltage_t *voltage);

/**
 * 采样并换算 SOC（结果缓存在模块内）。
 * @return TRUE 采样成功
 */
bool_t battery_percent_update(void);

/** 读取上次 battery_percent_update 的结果 */
bool_t battery_info_read(battery_info_t *info, battery_voltage_t *voltage);

/**
 * 一次完成：采样 + mV→百分比换算。
 * @return 0..100；失败返回 BATTERY_PERCENT_UNKNOWN
 */
uint8_t battery_get_percent(void);

#endif
