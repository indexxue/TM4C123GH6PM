/**
 * @file bsp_uart.h
 * @brief TM4C123 UART 薄封装
 */

#ifndef BSP_DRIVER_UART_H
#define BSP_DRIVER_UART_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t base;
    uint32_t baud_rate;
} bsp_uart_config_t;

bool bsp_uart_init(const bsp_uart_config_t *cfg);
void bsp_uart_putc(uint32_t base, char c);
void bsp_uart_puts(uint32_t base, const char *s);
int bsp_uart_getc(uint32_t base, char *c);

void bsp_uart_debug_init(uint32_t baud_rate);
void bsp_uart_debug_putc(char c);
void bsp_uart_debug_puts(const char *s);
void bsp_uart_debug_flush(void);

#endif /* BSP_DRIVER_UART_H */
