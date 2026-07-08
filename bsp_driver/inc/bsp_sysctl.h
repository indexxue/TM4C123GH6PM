/**
 * @file bsp_sysctl.h
 * @brief TM4C123 系统控制 / 时钟（TivaWare SysCtl 薄封装）
 */

#ifndef BSP_DRIVER_SYSCTL_H
#define BSP_DRIVER_SYSCTL_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    BSP_CLOCK_INT_PIOSC = 0,
    BSP_CLOCK_MAIN_8MHZ = 1,
} bsp_clock_source_t;

/** 8 MHz 主晶振 + PLL → 80 MHz 系统时钟（全项目统一） */
#define BSP_SYSCLK_HZ           80000000U

void bsp_clock_init(bsp_clock_source_t source);
uint32_t bsp_clock_get_hz(void);
bool bsp_periph_wait_ready(uint32_t periph, uint32_t timeout_us);

#endif /* BSP_DRIVER_SYSCTL_H */
