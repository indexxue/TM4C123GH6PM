/**
 * \file    app.c
 * \brief   双线程应用：主线程 + 从线程
 *
 * 主线程（primary）：上电灯效触发、50 Hz 控制环等关键逻辑。
 * 从线程（aux）：灯效帧刷新等辅助工作。
 */

#include "app.h"

#include <stdbool.h>

#include "FreeRTOS.h"
#include "task.h"

#include "led_scene.h"
#include "log.h"

#define TASK_PRIO_PRIMARY   (3U)
#define TASK_PRIO_AUX       (2U)

#define PRIMARY_STACK_WORDS (512U)
#define AUX_STACK_WORDS     (512U)

#define CONTROL_PERIOD_MS   (20U)

static void PrimaryTask(void *pvParameters);
static void AuxTask(void *pvParameters);

void App_Start(void)
{
    (void)xTaskCreate(AuxTask, "aux",
                      AUX_STACK_WORDS, NULL,
                      TASK_PRIO_AUX, NULL);

    (void)xTaskCreate(PrimaryTask, "primary",
                      PRIMARY_STACK_WORDS, NULL,
                      TASK_PRIO_PRIMARY, NULL);
}

static void PrimaryTask(void *pvParameters)
{
    TickType_t last_wake = xTaskGetTickCount();

    (void)pvParameters;

    if (led_scene_init() != STATUS_OK) {
        LOG_ERROR("led_scene_init failed");
    } else {
        led_scene_run(LED_SCENE_ID_BOOTUP);
        LOG_INFO("led_scene: bootup started");
    }

    for (;;) {
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(CONTROL_PERIOD_MS));

        /* TODO: 读取蓝牙指令 */
        /* TODO: IMU 姿态更新 */
        /* TODO: 编码器速度计算 */
        /* TODO: PID 控制器 */
        /* TODO: 输出电机 PWM */
    }
}

static void AuxTask(void *pvParameters)
{
    const TickType_t period = pdMS_TO_TICKS(LED_SCENE_TICK_MS);

    (void)pvParameters;

    for (;;) {
        led_scene_update();
        vTaskDelay(period);
    }
}
