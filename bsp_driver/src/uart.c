/**
 * @file uart.c
 * @brief UART7 调试串口（PE1 TX）
 */

#include "uart.h"

#include "clock.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "driverlib/gpio.h"
#include "driverlib/pin_map.h"
#include "driverlib/sysctl.h"
#include "driverlib/uart.h"
#include "inc/hw_memmap.h"

void bsp_uart_debug_init(uint32_t baud_rate)
{
    if (baud_rate == 0U) {
        return;
    }

    SysCtlPeripheralEnable(SYSCTL_PERIPH_UART7);
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOE);

    while (!SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOE)) {
    }

    GPIOPinConfigure(GPIO_PE1_U7TX);
    GPIOPinTypeUART(GPIO_PORTE_BASE, GPIO_PIN_1);

    UARTConfigSetExpClk(UART7_BASE, bsp_clock_get_hz(), baud_rate,
                        UART_CONFIG_WLEN_8 | UART_CONFIG_STOP_ONE | UART_CONFIG_PAR_NONE);
    UARTFIFOEnable(UART7_BASE);
    UARTEnable(UART7_BASE);
}

void bsp_uart_debug_putc(char c)
{
    UARTCharPut(UART7_BASE, c);
}

void bsp_uart_debug_puts(const char *s)
{
    if (s == NULL) {
        return;
    }

    while (*s != '\0') {
        bsp_uart_debug_putc(*s++);
    }
}
