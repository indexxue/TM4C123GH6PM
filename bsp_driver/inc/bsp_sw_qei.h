/**
 * @file bsp_sw_qei.h
 * @brief GPIO 边沿中断 + 轮询备份软件正交解码（M3/M4 等无硬件 QEI 通道）
 */

#ifndef BSP_DRIVER_SW_QEI_H
#define BSP_DRIVER_SW_QEI_H

#include <stdbool.h>
#include <stdint.h>

#include "bsp_gpio.h"

typedef struct {
    bsp_gpio_pin_t pin_a;
    bsp_gpio_pin_t pin_b;
} bsp_sw_qei_channel_t;

bool bsp_sw_qei_register(uint8_t index, const bsp_sw_qei_channel_t *ch);
void bsp_sw_qei_enable(void);
int32_t bsp_sw_qei_get_count(uint8_t index);
void bsp_sw_qei_reset(uint8_t index);
void bsp_sw_qei_poll(uint8_t index);
void bsp_sw_qei_poll_all(void);
uint8_t bsp_sw_qei_read_ab(uint8_t index);

#endif /* BSP_DRIVER_SW_QEI_H */
