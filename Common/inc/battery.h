/**
 * @file battery.h
 * @brief TM4C123 电池电压采样（ADC1 AIN0 @ PE3，1MΩ+200kΩ 分压，3S 18650 标称 12V）
 */

#ifndef BATTERY_H
#define BATTERY_H

#include "type.h"

#include <stdint.h>

#define BATTERY_LEVEL_NUM (4U)

typedef struct {
    uint16_t min_mv;
    uint16_t max_mv;
    uint16_t current_mv;
} battery_voltage_t;

typedef struct {
    uint8_t percent;
    uint8_t level;
    bool_t charging;
} battery_info_t;

void battery_init(void);
uint32_t battery_voltage_read_mv(battery_voltage_t *voltage);
bool_t battery_percent_update(void);
bool_t battery_info_read(battery_info_t *info, battery_voltage_t *voltage);

#endif
