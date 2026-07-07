/**
 * @file clock.h
 * @brief TM4C123 系统时钟初始化（TivaWare SysCtl 薄封装）
 */

#ifndef BSP_DRIVER_CLOCK_H
#define BSP_DRIVER_CLOCK_H

#include <stdint.h>

typedef enum {
    BSP_CLOCK_INT_PIOSC = 0,
    BSP_CLOCK_MAIN_8MHZ = 1,
    BSP_CLOCK_MAIN_16MHZ = 2,
} bsp_clock_source_t;

void bsp_clock_init(bsp_clock_source_t source);
uint32_t bsp_clock_get_hz(void);

#endif /* BSP_DRIVER_CLOCK_H */
