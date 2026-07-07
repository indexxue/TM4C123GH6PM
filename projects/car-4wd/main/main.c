/**
 * \file    main.c
 * \brief   TM4C123GH6PM 小车底盘入口 (FreeRTOS)
 *
 * 外设初始化 → App_Start → vTaskStartScheduler
 */

#include "FreeRTOS.h"
#include "task.h"

#include "app.h"
#include "start.h"

int main(void)
{
    Start_Init();
    App_Start();
    vTaskStartScheduler();

    for (;;) {
    }
}
