/**
 * @file    factory_app.c
 * @brief   厂测应用：主线程心跳
 */

#include "factory.h"

#include "FreeRTOS.h"
#include "task.h"

#include "log.h"

#define FACTORY_VERSION         "ft-0.1.0"

#define TASK_PRIO_PRIMARY       (3U)
#define PRIMARY_STACK_WORDS     (512U)
#define FACTORY_HEARTBEAT_MS    (1000U)

static void PrimaryTask(void *pvParameters);

void Factory_Start(void)
{
    (void)xTaskCreate(PrimaryTask, "factory_primary",
                      PRIMARY_STACK_WORDS, NULL,
                      TASK_PRIO_PRIMARY, NULL);
}

static void PrimaryTask(void *pvParameters)
{
    (void)pvParameters;

    LOG_INFO("factory:ready version=%s", FACTORY_VERSION);

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(FACTORY_HEARTBEAT_MS));
        LOG_INFO("factory:heartbeat");
    }
}
