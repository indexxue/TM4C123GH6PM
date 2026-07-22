/**
 * @file    main.c
 * @brief   厂测固件入口（FreeRTOS）
 *
 * 产物 factory.bin 烧录至 APP_B（0x00021000），Boot 按 NVS slot 直接跳转。
 * 详见 projects/factory/README.md、PARTITION.md
 */

#include "FreeRTOS.h"
#include "task.h"

#include "factory.h"

#include "bsp_uart.h"
#include "type.h"

int main(void)
{
    Factory_Board_Init();
    if (Factory_Start() != STATUS_OK) {
        bsp_uart_debug_puts("[factory] start FAILED, halt\r\n");
        for (;;) {
        }
    }
    vTaskStartScheduler();

    bsp_uart_debug_puts("[factory] scheduler FAILED\r\n");
    for (;;) {
    }
}
