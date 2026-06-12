/**
 * @file    factory_app.c
 * @brief   厂测双线程应用：主线程 + 从线程
 *
 * 主线程（factory_primary）：上电灯效触发、厂测主循环（心跳、后续测试项）。
 * 从线程（factory_aux）：灯效帧刷新。
 */

#include "factory.h"

#include "FreeRTOS.h"
#include "task.h"

#include "led_scene.h"
#include "log.h"

#define FACTORY_VERSION         "ft-0.1.0"

#define TASK_PRIO_PRIMARY       (3U)
#define TASK_PRIO_AUX           (2U)

#define PRIMARY_STACK_WORDS     (512U)
#define AUX_STACK_WORDS         (512U)

#define FACTORY_HEARTBEAT_MS    (1000U)

static void PrimaryTask(void *pvParameters);
static void AuxTask(void *pvParameters);

void Factory_Start(void)
{
    (void)xTaskCreate(AuxTask, "factory_aux",
                      AUX_STACK_WORDS, NULL,
                      TASK_PRIO_AUX, NULL);

    (void)xTaskCreate(PrimaryTask, "factory_primary",
                      PRIMARY_STACK_WORDS, NULL,
                      TASK_PRIO_PRIMARY, NULL);
}

static void PrimaryTask(void *pvParameters)
{
    (void)pvParameters;

    if (led_scene_init() != STATUS_OK) {
        LOG_ERROR("factory: led_scene_init failed");
    } else {
        led_scene_run(LED_SCENE_ID_BOOTUP);
        LOG_INFO("factory:ready version=%s", FACTORY_VERSION);
    }

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(FACTORY_HEARTBEAT_MS));
        LOG_INFO("factory:heartbeat");
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
