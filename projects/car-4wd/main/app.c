/**
 * @file    app.c
 * @brief   car-4wd 应用：app_evt 事件调度 + app_tmr 周期定时 + 2s 串口心跳
 */

#include "app.h"

#include "attitude.h"
#include "board.h"
#include "battery.h"
#include "button.h"
#include "buzzer.h"
#include "cfg.h"
#include "cmd.h"
#include "device_profile.h"
#include "event.h"
#include "flash_layout.h"
#include "imu.h"
#include "log.h"
#include "magnetometer.h"
#include "nvs.h"

#include "bsp_uart.h"

#include "FreeRTOS.h"
#include "task.h"

#include <stdio.h>

/* -------------------------------------------------------------------------- */
/* 任务参数                                                                   */
/* -------------------------------------------------------------------------- */

#define APP_TASK_PRIO_EVT           (3U)
#define APP_TASK_PRIO_TMR           (2U)

#define APP_EVT_STACK_WORDS         (768U)
#define APP_TMR_STACK_WORDS         (256U)

#define APP_CTRL_PERIOD_MS          (20U)
#define APP_HEARTBEAT_PERIOD_MS     (2000U)

/** 姿态解算采样率 = 1 / app_tmr 周期 */
#define APP_ATT_SAMPLE_HZ           (1000.0f / (float)APP_CTRL_PERIOD_MS)
/** 每 N 个控制周期打印一次姿态（20ms * 25 = 500ms） */
#define APP_ATT_LOG_INTERVAL        (25U)

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
/* IMU / 磁力计 / 姿态（I2C0 软件 I2C，地址见 imu.h / magnetometer.h）         */
/* -------------------------------------------------------------------------- */

static void app_sensors_init(void)
{
    imu_sample_t imu;
    magnetometer_sample_t mag;
    status_t st;

    if (!device_profile_board_wants(DEVICE_BOARD_MASK_PERIPH)) {
        return;
    }

    st = imu_init();
    if (st != STATUS_OK) {
        LOG_WARN("app:imu init failed (%d)", (int)st);
        return;
    }
    LOG_INFO("app:imu ready addr=0x%02X", (unsigned)IMU_I2C_ADDR_DEFAULT);

    st = magnetometer_init();
    if (st != STATUS_OK) {
        LOG_WARN("app:mag init failed (%d)", (int)st);
        return;
    }
    LOG_INFO("app:mag ready addr=0x%02X", (unsigned)MAGNETOMETER_I2C_ADDR_DEFAULT);

    if (attitude_init(APP_ATT_SAMPLE_HZ) != STATUS_OK) {
        LOG_WARN("app:att init failed");
        return;
    }
    LOG_INFO("app:att madgwick ready %dHz 9dof", (int)APP_ATT_SAMPLE_HZ);

    if ((imu_read_sample(&imu) == STATUS_OK) && (magnetometer_read_sample(&mag) == STATUS_OK) &&
        (attitude_update_from_sensors(&imu, &mag) == STATUS_OK)) {
        attitude_euler_t euler;

        if (attitude_get_euler(&euler) == STATUS_OK) {
            LOG_INFO("app:att roll=%d pitch=%d yaw=%d (0.1deg)",
                     (int)euler.roll_x10, (int)euler.pitch_x10, (int)euler.yaw_x10);
        }
    }
}

static void app_attitude_periodic(void)
{
    static uint32_t s_log_div;

    imu_sample_t imu;
    magnetometer_sample_t mag;
    attitude_euler_t euler;

    if (!attitude_is_ready()) {
        return;
    }
    if ((imu_read_sample(&imu) != STATUS_OK) || (magnetometer_read_sample(&mag) != STATUS_OK)) {
        return;
    }
    if (attitude_update_from_sensors(&imu, &mag) != STATUS_OK) {
        return;
    }

    if (!device_profile_platform_wants(DEVICE_PLATFORM_MASK_LOG)) {
        return;
    }

    s_log_div++;
    if (s_log_div < APP_ATT_LOG_INTERVAL) {
        return;
    }
    s_log_div = 0U;

    if (attitude_get_euler(&euler) == STATUS_OK) {
        LOG_INFO("app:att roll=%d pitch=%d yaw=%d (0.1deg)",
                 (int)euler.roll_x10, (int)euler.pitch_x10, (int)euler.yaw_x10);
    }
}

static void app_cmd_att(int argc, const char *argv[])
{
    imu_sample_t imu;
    magnetometer_sample_t mag;
    attitude_euler_t euler;
    char buf[CMD_STATUS_BUF_SIZE];

    (void)argc;
    (void)argv;

    if (!attitude_is_ready()) {
        cmd_reply_ok("att", "ng:not ready");
        return;
    }
    if ((imu_read_sample(&imu) != STATUS_OK) || (magnetometer_read_sample(&mag) != STATUS_OK)) {
        cmd_reply_ok("att", "ng:sensor read");
        return;
    }
    if (attitude_update_from_sensors(&imu, &mag) != STATUS_OK) {
        cmd_reply_ok("att", "ng:fusion");
        return;
    }
    if (attitude_get_euler(&euler) != STATUS_OK) {
        cmd_reply_ok("att", "ng:euler");
        return;
    }

    (void)snprintf(buf, sizeof(buf), "roll=%d pitch=%d yaw=%d (0.1deg)",
                   (int)euler.roll_x10, (int)euler.pitch_x10, (int)euler.yaw_x10);
    cmd_reply_ok("att", buf);
}

static void app_cmd_register(void)
{
    (void)cmd_register("att", app_cmd_att, "att read euler angles (0.1deg)");
}

/* -------------------------------------------------------------------------- */
/* 业务钩子（本文件内 static，按需扩展）                                       */
/* -------------------------------------------------------------------------- */

static void app_user_init(void)
{
    battery_init();
    buzzer_init();
    buzzer_chirp(2U, BUZZER_DEFAULT_ON_MS, BUZZER_DEFAULT_GAP_MS);
    app_sensors_init();

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

    app_attitude_periodic();

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
    app_cmd_register();

    return STATUS_OK;
}
