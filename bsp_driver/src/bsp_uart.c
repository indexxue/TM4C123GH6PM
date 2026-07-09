/**
 * @file bsp_uart.c
 * @brief TM4C123 UART
 */

#include "bsp_uart.h"

#include "bsp_sysctl.h"

#include "bsp_config.h"

#include "driverlib/sysctl.h"
#include "driverlib/uart.h"
#include "inc/hw_memmap.h"

#define BSP_UART_PERIPH_READY_US 100000U

static uint32_t s_debug_uart_base;
static bool s_debug_uart_ready;

static uint32_t uart_periph_from_base(uint32_t base)
{
    switch (base) {
    case UART0_BASE:
        return SYSCTL_PERIPH_UART0;
    case UART1_BASE:
        return SYSCTL_PERIPH_UART1;
    case UART2_BASE:
        return SYSCTL_PERIPH_UART2;
    case UART3_BASE:
        return SYSCTL_PERIPH_UART3;
    case UART4_BASE:
        return SYSCTL_PERIPH_UART4;
    case UART5_BASE:
        return SYSCTL_PERIPH_UART5;
    case UART6_BASE:
        return SYSCTL_PERIPH_UART6;
    case UART7_BASE:
        return SYSCTL_PERIPH_UART7;
    default:
        return 0U;
    }
}

bool bsp_uart_init(const bsp_uart_config_t *cfg)
{
    uint32_t periph;

    if ((cfg == NULL) || (cfg->base == 0U) || (cfg->baud_rate == 0U)) {
        return false;
    }

    periph = uart_periph_from_base(cfg->base);
    if (periph == 0U) {
        return false;
    }

    SysCtlPeripheralEnable(periph);
    if (!bsp_periph_wait_ready(periph, BSP_UART_PERIPH_READY_US)) {
        return false;
    }

    UARTConfigSetExpClk(cfg->base, bsp_clock_get_hz(), cfg->baud_rate,
                        UART_CONFIG_WLEN_8 | UART_CONFIG_STOP_ONE | UART_CONFIG_PAR_NONE);
    UARTFIFOEnable(cfg->base);
    UARTEnable(cfg->base);

    if (cfg->base == UART7_BASE) {
        s_debug_uart_base = UART7_BASE;
        s_debug_uart_ready = true;
    }

    return true;
}

void bsp_uart_putc(uint32_t base, char c)
{
    UARTCharPut(base, c);
}

void bsp_uart_puts(uint32_t base, const char *s)
{
    if (s == NULL) {
        return;
    }

    while (*s != '\0') {
        bsp_uart_putc(base, *s++);
    }
}

int bsp_uart_getc(uint32_t base, char *c)
{
    if ((c == NULL) || !UARTCharsAvail(base)) {
        return 0;
    }

    *c = (char)UARTCharGetNonBlocking(base);
    return 1;
}

void bsp_uart_debug_init(uint32_t baud_rate)
{
    bsp_uart_config_t cfg = {
        .base = UART7_BASE,
        .baud_rate = baud_rate,
    };

    s_debug_uart_base = UART7_BASE;
    s_debug_uart_ready = bsp_uart_init(&cfg);
}

void bsp_uart_debug_putc(char c)
{
    if (!s_debug_uart_ready) {
        return;
    }
    bsp_uart_putc(s_debug_uart_base, c);
}

void bsp_uart_debug_puts(const char *s)
{
    if (!s_debug_uart_ready) {
        return;
    }
    bsp_uart_puts(s_debug_uart_base, s);
}

void bsp_uart_debug_flush(void)
{
    if (!s_debug_uart_ready) {
        return;
    }

    while (UARTBusy(s_debug_uart_base)) {
    }
}
