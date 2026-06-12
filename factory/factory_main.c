/**
 * @file    factory_main.c
 * @brief   厂测固件入口（FreeRTOS）
 *
 * 产物 factory.bin 烧录至 APP_B（0x00021000），经 ftmenter 激活后由 Boot 搬运至 APP_A 运行。
 * 详见 factory/README.md、PARTITION.md
 */

#include "FreeRTOS.h"
#include "task.h"

#include "factory.h"

int main(void)
{
    Factory_Board_Init();
    Factory_Start();
    vTaskStartScheduler();

    for (;;) {
    }
}
