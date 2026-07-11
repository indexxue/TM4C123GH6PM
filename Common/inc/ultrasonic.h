/**
 * @file    ultrasonic.h
 * @brief   HC-SR04 超声波测距公共模块（Trig/Echo GPIO，延迟初始化 Board_Ultra_Init）
 */

#ifndef ULTRASONIC_H
#define ULTRASONIC_H

#include "type.h"

#include <stdint.h>

status_t ultrasonic_init(void);
bool_t ultrasonic_is_ready(void);
status_t ultrasonic_measure_mm(uint16_t *distance_mm);
status_t ultrasonic_measure_cm(uint8_t *distance_cm);

#endif /* ULTRASONIC_H */
