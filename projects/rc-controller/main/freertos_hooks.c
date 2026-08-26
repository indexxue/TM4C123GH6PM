/**
 * \file    freertos_hooks.c
 * \brief   FreeRTOS 应用回调钩子
 */

#include "FreeRTOS.h"
#include "task.h"

#include "bsp_uart.h"

#include "driverlib/uart.h"
#include "inc/hw_memmap.h"

static void rc_fatal_hang(const char *msg, const char *detail)
{
    uint32_t base = UART7_BASE;

    taskDISABLE_INTERRUPTS();
    bsp_uart_puts(base, "\r\n[FATAL] ");
    if (msg != NULL) {
        bsp_uart_puts(base, msg);
    }
    if ((detail != NULL) && (detail[0] != '\0')) {
        bsp_uart_puts(base, " ");
        bsp_uart_puts(base, detail);
    }
    bsp_uart_puts(base, "\r\n");
    for (;;) {
    }
}

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void)xTask;
    rc_fatal_hang("stack overflow", pcTaskName);
}

void vApplicationMallocFailedHook(void)
{
    rc_fatal_hang("malloc failed", NULL);
}

void vApplicationIdleHook(void)
{
}
