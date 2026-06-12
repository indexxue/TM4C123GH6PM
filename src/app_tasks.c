/**
 * \file    app_tasks.c
 * \brief   小车应用 FreeRTOS 任务
 */

#include <stdint.h>
#include <stdbool.h>
#include "FreeRTOS.h"
#include "task.h"
#include "inc/hw_memmap.h"
#include "driverlib/gpio.h"
#include "app_tasks.h"

/* 任务优先级：数值越大越高 */
#define TASK_PRIO_CONTROL   (3U)
#define TASK_PRIO_HEARTBEAT (1U)

#define CONTROL_STACK_WORDS   (512U)
#define HEARTBEAT_STACK_WORDS (128U)

#define CONTROL_PERIOD_MS   (20U)   /* 50 Hz */
#define HEARTBEAT_PERIOD_MS (500U)

static void ControlTask(void *pvParameters);
static void HeartbeatTask(void *pvParameters);

void AppTasks_Create(void)
{
    (void)xTaskCreate(ControlTask, "control",
                      CONTROL_STACK_WORDS, NULL,
                      TASK_PRIO_CONTROL, NULL);

    (void)xTaskCreate(HeartbeatTask, "heartbeat",
                      HEARTBEAT_STACK_WORDS, NULL,
                      TASK_PRIO_HEARTBEAT, NULL);
}

static void ControlTask(void *pvParameters)
{
    TickType_t last_wake = xTaskGetTickCount();

    (void)pvParameters;

    for (;;) {
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(CONTROL_PERIOD_MS));

        /* TODO: 读取蓝牙指令 */
        /* TODO: IMU 姿态更新 */
        /* TODO: 编码器速度计算 */
        /* TODO: PID 控制器 */
        /* TODO: 输出电机 PWM */
    }
}

static void HeartbeatTask(void *pvParameters)
{
    (void)pvParameters;

    for (;;) {
        GPIOPinWrite(GPIO_PORTF_BASE, GPIO_PIN_1,
            ~GPIOPinRead(GPIO_PORTF_BASE, GPIO_PIN_1) & GPIO_PIN_1);
        vTaskDelay(pdMS_TO_TICKS(HEARTBEAT_PERIOD_MS));
    }
}
