/**
 * @file bsp_dac.h
 * @brief TM4C123 模拟输出薄封装（PWM 软 DAC；本芯片无片上 DAC）
 */

#ifndef BSP_DRIVER_DAC_H
#define BSP_DRIVER_DAC_H

#include <stdint.h>

#include "bsp_timer.h"

typedef struct {
    bsp_pwm_channel_t pwm;
    uint32_t clock_hz;
    uint8_t resolution_bits;
} bsp_dac_config_t;

bool bsp_dac_init(const bsp_dac_config_t *cfg);
void bsp_dac_set_value(const bsp_dac_config_t *cfg, uint32_t value);

#endif /* BSP_DRIVER_DAC_H */
