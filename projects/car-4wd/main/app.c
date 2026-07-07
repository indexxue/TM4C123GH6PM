/**
 * \file    app.c
 * \brief   应用任务：按 device_profile 的 platform_mask 拉起任务
 */

#include "app.h"

#include "device_profile.h"
#include "log.h"

#include "FreeRTOS.h"
#include "task.h"

#ifndef FIRMWARE_PROFILE_LOG_ONLY
#define FIRMWARE_PROFILE_LOG_ONLY 0
#endif

#if !FIRMWARE_PROFILE_LOG_ONLY
#include "cmd.h"
#include "led_scene.h"
#include "motor.h"
#endif

#define TASK_PRIO_PRIMARY   (3U)
#define TASK_PRIO_AUX       (2U)

#define PRIMARY_STACK_WORDS (512U)
#define AUX_STACK_WORDS     (512U)

#define CONTROL_PERIOD_MS   (20U)
#define LOG_HEARTBEAT_MS    (1000U)

static void PrimaryTask(void *pvParameters);
#if !FIRMWARE_PROFILE_LOG_ONLY
static void AuxTask(void *pvParameters);
#endif

void App_Start(void)
{
#if !FIRMWARE_PROFILE_LOG_ONLY
    if (device_profile_platform_wants(DEVICE_PLATFORM_MASK_LED)) {
        (void)xTaskCreate(AuxTask, "aux",
                          AUX_STACK_WORDS, NULL,
                          TASK_PRIO_AUX, NULL);
    }
    if (device_profile_platform_wants(DEVICE_PLATFORM_MASK_CMD)) {
        (void)cmd_uart_line_service_start();
    }
#endif

    (void)xTaskCreate(PrimaryTask, "primary",
                      PRIMARY_STACK_WORDS, NULL,
                      TASK_PRIO_PRIMARY, NULL);
}

static void PrimaryTask(void *pvParameters)
{
    TickType_t last_wake = xTaskGetTickCount();
    TickType_t last_log = xTaskGetTickCount();
    uint32_t heartbeat = 0U;

    (void)pvParameters;

    if (device_profile_platform_wants(DEVICE_PLATFORM_MASK_LOG)) {
        log_notify_scheduler_running();
        (void)log_set_level(LOG_LEVEL_VERBOSE);
        LOG_INFO("app: started (%s)", device_profile_product()->name);
    }

#if !FIRMWARE_PROFILE_LOG_ONLY
    if (device_profile_platform_wants(DEVICE_PLATFORM_MASK_LED)) {
        if (led_scene_init() != STATUS_OK) {
            LOG_ERROR("led_scene_init failed");
        } else {
            led_scene_run(LED_SCENE_ID_BOOTUP);
            LOG_INFO("led_scene: bootup started");
        }
    }

    if (device_profile_board_wants(DEVICE_BOARD_MASK_MOTOR)) {
        LOG_INFO("app: motor open-loop demo 3s @ M1/M2");
        Motor_SetSpeed(1U, 100);
        Motor_SetSpeed(2U, 100);
        vTaskDelay(pdMS_TO_TICKS(3000U));
        Motor_SetSpeed(1U, 0);
        Motor_SetSpeed(2U, 0);
        LOG_INFO("app: motor demo done");
    }
#endif

    for (;;) {
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(CONTROL_PERIOD_MS));

        if (device_profile_platform_wants(DEVICE_PLATFORM_MASK_LOG)) {
            if ((xTaskGetTickCount() - last_log) >= pdMS_TO_TICKS(LOG_HEARTBEAT_MS)) {
                last_log = xTaskGetTickCount();
                heartbeat++;
                LOG_INFO("heartbeat #%lu", (unsigned long)heartbeat);
            }
        }

#if !FIRMWARE_PROFILE_LOG_ONLY
        /* TODO: 读取蓝牙指令 */
        /* TODO: IMU 姿态更新 */
        /* TODO: 编码器速度计算 */
        /* TODO: PID 控制器 */
        /* TODO: 输出电机 PWM */
#endif
    }
}

#if !FIRMWARE_PROFILE_LOG_ONLY
static void AuxTask(void *pvParameters)
{
    const TickType_t period = pdMS_TO_TICKS(LED_SCENE_TICK_MS);

    (void)pvParameters;

    for (;;) {
        led_scene_update();
        vTaskDelay(period);
    }
}
#endif
