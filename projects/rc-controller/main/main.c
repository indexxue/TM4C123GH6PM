/**
 * \file    main.c
 * \brief   TM4C123GH6PM 遥控器入口 (FreeRTOS)
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
