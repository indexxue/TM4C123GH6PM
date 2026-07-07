/**
 * \file    freertos_hooks.c
 * \brief   FreeRTOS 应用回调钩子
 */

#include "FreeRTOS.h"
#include "task.h"

#include "bsp_uart.h"

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void)xTask;
    bsp_uart_debug_puts("[rtos] stack overflow: ");
    if (pcTaskName != NULL) {
        bsp_uart_debug_puts(pcTaskName);
    }
    bsp_uart_debug_puts("\r\n");
    taskDISABLE_INTERRUPTS();
    for (;;) {
    }
}

void vApplicationMallocFailedHook(void)
{
    bsp_uart_debug_puts("[rtos] malloc failed (heap exhausted)\r\n");
    taskDISABLE_INTERRUPTS();
    for (;;) {
    }
}

void vApplicationIdleHook(void)
{
}
