/**
 * \file    main.c
 * \brief   TM4C123GH6PM 小车底盘入口 (FreeRTOS)
 *
 * init.c 负责板级与外设初始化；app.c 负责 FreeRTOS 任务与业务逻辑。
 * 详见 docs/resource-allocation-pro.md
 */

#include "FreeRTOS.h"
#include "task.h"

#include "init.h"
#include "app.h"

int main(void)
{
    Board_Init();
    App_Start();
    vTaskStartScheduler();

    for (;;) {
    }
}
