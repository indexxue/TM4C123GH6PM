/**
 * \file    main.c
 * \brief   TM4C123GH6PM 小车底盘入口 (FreeRTOS)
 *
 * start.c（Common）负责平台初始化；app.c 负责 FreeRTOS 任务与业务逻辑。
 * 详见 docs/resource-allocation.md
 */

#include "FreeRTOS.h"
#include "task.h"

#include "start.h"
#include "app.h"

int main(void)
{
    Start_Init();
    App_Start();
    vTaskStartScheduler();

    for (;;) {
    }
}
