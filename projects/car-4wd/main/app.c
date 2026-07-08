/**
 * @file    app.c
 * @brief   car-4wd 应用：app_evt 事件调度 + app_tmr 周期定时 + 2s 串口心跳
 */

#include "app.h"

#include "board.h"
#include "battery.h"
#include "button.h"
#include "buzzer.h"
#include "cfg.h"
#include "cmd.h"
#include "device_profile.h"
#include "event.h"
#include "flash_layout.h"
#include "log.h"
#include "nvs.h"

#include "bsp_uart.h"

#include "FreeRTOS.h"
#include "task.h"

/* -------------------------------------------------------------------------- */
/* 任务参数                                                                   */
/* -------------------------------------------------------------------------- */

#define APP_TASK_PRIO_EVT           (3U)
#define APP_TASK_PRIO_TMR           (2U)

#define APP_EVT_STACK_WORDS         (768U)
#define APP_TMR_STACK_WORDS         (256U)

#define APP_CTRL_PERIOD_MS          (20U)
#define APP_HEARTBEAT_PERIOD_MS     (2000U)

/* -------------------------------------------------------------------------- */
/* 心跳（在 app_evt 任务上下文采样，避免定时器回调里读 ADC）                   */
/* -------------------------------------------------------------------------- */

static uint32_t s_heartbeat_count;
static uint32_t s_heartbeat_elapsed_ms;

static void app_heartbeat_log(void)
{
    battery_voltage_t batt = {0};
    battery_info_t info = {0};

    s_heartbeat_count++;
    if (battery_percent_update()) {
        (void)battery_info_read(&info, &batt);
    }

    LOG_INFO("heartbeat #%lu batt=%lu.%03lu V %u%%",
             (unsigned long)s_heartbeat_count,
             (unsigned long)(batt.current_mv / 1000U),
             (unsigned long)(batt.current_mv % 1000U),
             (unsigned)info.percent);
}

/* -------------------------------------------------------------------------- */
/* 业务钩子（本文件内 static，按需扩展）                                       */
/* -------------------------------------------------------------------------- */

static void app_user_init(void)
{
    battery_init();
    buzzer_init();
    buzzer_chirp(2U, BUZZER_DEFAULT_ON_MS, BUZZER_DEFAULT_GAP_MS);

    if (device_profile_board_wants(DEVICE_BOARD_MASK_MOTOR)) {
        LOG_INFO("app: motor open-loop demo 3s @ M1/M2");
        Motor_SetSpeed(1U, cfg_motor_rpm(1U, 100));
        Motor_SetSpeed(2U, cfg_motor_rpm(2U, 100));
        vTaskDelay(pdMS_TO_TICKS(3000U));
        Motor_SetSpeed(1U, 0);
        Motor_SetSpeed(2U, 0);
        LOG_INFO("app: motor demo done");
    }
}

static void app_on_timer(void)
{
    if (device_profile_platform_wants(DEVICE_PLATFORM_MASK_LOG)) {
        s_heartbeat_elapsed_ms += APP_CTRL_PERIOD_MS;
        if (s_heartbeat_elapsed_ms >= APP_HEARTBEAT_PERIOD_MS) {
            s_heartbeat_elapsed_ms -= APP_HEARTBEAT_PERIOD_MS;
            app_heartbeat_log();
        }
    }

    /* TODO: IMU 姿态更新 */
    /* TODO: 编码器速度计算（cfg_encoder_count + cfg_kinematics） */
    /* TODO: PID 控制器（cfg_pid_speed / cfg_pid_line） */
    /* TODO: 输出电机 PWM（cfg_motor_rpm + cfg_spd_limit 限速） */
}

static void app_on_button(void)
{
    /* TODO: 按键业务 */
}

static void app_on_input(void)
{
    /* TODO: 蓝牙协议层指令处理（UART0） */
}

/* -------------------------------------------------------------------------- */
/* 按键 → EVT_ID_BUTTON                                                       */
/* -------------------------------------------------------------------------- */

static void app_button_notify(btn_id_e id, const char *name, btn_permission_e permission, btn_event_e event)
{
    if ((event == BTN_EVENT_LONG_PRESS) && ((permission & BTN_PERMISSION_FTM) != 0U)) {
        (void)cmd_boot_slot_switch(BOOT_SLOT_B);
    }
    button_log_notify(id, name, permission, event);
    event_set(EVT_ID_BUTTON);
}

/* -------------------------------------------------------------------------- */
/* app_evt：阻塞 event_schedule，按位分发                                     */
/* -------------------------------------------------------------------------- */

static void app_evt_dispatch_task(void *arg)
{
    (void)arg;

    if (device_profile_platform_wants(DEVICE_PLATFORM_MASK_LOG)) {
        log_notify_scheduler_running();
        (void)log_set_level(LOG_LEVEL_VERBOSE);
    }

    if (nvs_startup_finalize() != STATUS_OK) {
        LOG_WARN("app: nvs_startup_finalize failed");
    }

    app_user_init();

    for (;;) {
        event_schedule();

        if (event_is_set(EVT_ID_TIMER)) {
            if (device_profile_platform_wants(DEVICE_PLATFORM_MASK_BUTTON)) {
                button_schedule();
            }

            app_on_timer();
        }

        if (event_is_set(EVT_ID_BUTTON)) {
            app_on_button();
        }

        if (event_is_set(EVT_ID_INPUT)) {
            app_on_input();
        }

        if (event_is_set(EVT_ID_WATCHDOG)) {
            /* 预留：看门狗/健康检查 */
        }
    }
}

/* -------------------------------------------------------------------------- */
/* app_tmr：周期投递 EVT_ID_TIMER                                             */
/* -------------------------------------------------------------------------- */

static void app_tmr_tick_task(void *arg)
{
    TickType_t last_wake = xTaskGetTickCount();

    (void)arg;

    for (;;) {
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(APP_CTRL_PERIOD_MS));
        event_set(EVT_ID_TIMER);
    }
}

/* -------------------------------------------------------------------------- */
/* 对外入口                                                                   */
/* -------------------------------------------------------------------------- */

status_t App_Start(void)
{
    status_t st;

    if (nvs_init() != STATUS_OK) {
        bsp_uart_debug_puts("[app] nvs_init FAILED\r\n");
        return STATUS_FAIL;
    }

    cfg_init();

    st = event_init();
    if (st != STATUS_OK) {
        bsp_uart_debug_puts("[app] event_init FAILED\r\n");
        return st;
    }

    if (device_profile_platform_wants(DEVICE_PLATFORM_MASK_BUTTON)) {
        button_init(app_button_notify);
    }

    if (xTaskCreate(app_evt_dispatch_task, APP_TASK_NAME_EVT,
                    APP_EVT_STACK_WORDS, NULL,
                    APP_TASK_PRIO_EVT, NULL) != pdPASS) {
        bsp_uart_debug_puts("[app] app_evt create FAIL (heap)\r\n");
        return STATUS_NO_MEM;
    }

    if (xTaskCreate(app_tmr_tick_task, APP_TASK_NAME_TMR,
                    APP_TMR_STACK_WORDS, NULL,
                    APP_TASK_PRIO_TMR, NULL) != pdPASS) {
        bsp_uart_debug_puts("[app] app_tmr create FAIL (heap)\r\n");
        return STATUS_NO_MEM;
    }

    st = cmd_uart_line_service_start();
    if (st != STATUS_OK) {
        bsp_uart_debug_puts("[app] cmd service start FAILED\r\n");
        return st;
    }

    return STATUS_OK;
}
