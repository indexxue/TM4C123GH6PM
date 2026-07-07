/**
 * @file uart.h
 * @brief TM4C123 调试串口薄封装（UART7 TX @ PE1）
 */

#ifndef BSP_DRIVER_UART_H
#define BSP_DRIVER_UART_H

#include <stdint.h>

void bsp_uart_debug_init(uint32_t baud_rate);
void bsp_uart_debug_putc(char c);
void bsp_uart_debug_puts(const char *s);

#endif /* BSP_DRIVER_UART_H */
