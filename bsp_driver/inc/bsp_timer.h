/**
 * @file bsp_timer.h
 * @brief TM4C123 Timer（含 PWM）薄封装
 */

#ifndef BSP_DRIVER_TIMER_H
#define BSP_DRIVER_TIMER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t timer_base;
    uint32_t timer_periph;
    uint32_t channel;
    uint32_t frequency_hz;
} bsp_pwm_channel_t;

typedef struct {
    const bsp_pwm_channel_t *channels;
    size_t channel_count;
    uint32_t clock_hz;
} bsp_pwm_config_t;

bool bsp_pwm_init(const bsp_pwm_config_t *cfg);
void bsp_pwm_set_duty(uint32_t timer_base, uint32_t channel, uint16_t duty_permille);

#endif /* BSP_DRIVER_TIMER_H */
