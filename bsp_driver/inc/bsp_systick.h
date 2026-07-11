/**
 * @file bsp_systick.h
 * @brief 延时与时间戳（DWT 微秒 + FreeRTOS 毫秒）
 */

#ifndef BSP_DRIVER_SYSTICK_H
#define BSP_DRIVER_SYSTICK_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t start_cycles;
    uint32_t limit_cycles;
} bsp_timeout_t;

void bsp_systick_init(void);
void bsp_delay_us(uint32_t us);
void bsp_delay_ms(uint32_t ms);
uint32_t bsp_get_tick_ms(void);
/** Monotonic microsecond counter from DWT; returns 0 if DWT is not ready. */
uint32_t bsp_get_tick_us(void);

void bsp_timeout_start_us(bsp_timeout_t *t, uint32_t timeout_us);
bool bsp_timeout_expired(const bsp_timeout_t *t);
bool bsp_dwt_is_ready(void);

#endif /* BSP_DRIVER_SYSTICK_H */
