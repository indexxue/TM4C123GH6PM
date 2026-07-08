/**
 * @file    factory_app.c
 * @brief   厂测应用：主线程心跳
 */

#include "factory.h"

#include "button.h"
#include "cmd.h"

#include "boot_slot.h"

#include "FreeRTOS.h"
#include "task.h"

#include "log.h"

void factory_button_notify(btn_id_e id, const char *name, btn_permission_e permission,
                           btn_event_e event)
{
    if ((event == BTN_EVENT_LONG_PRESS) && ((permission & BTN_PERMISSION_FTM) != 0U)) {
        (void)cmd_boot_slot_switch(BOOT_SLOT_A);
    }
    button_log_notify(id, name, permission, event);
}

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
        button_schedule();
        vTaskDelay(pdMS_TO_TICKS(FACTORY_HEARTBEAT_MS));
        LOG_INFO("factory:heartbeat");
    }
}
