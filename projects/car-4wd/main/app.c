/**
 * @file    app.c
 * @brief   (car-4wd) app_evt 事件调度 + app_tmr 周期定时 + UART0 蓝牙协议层
 */

#include "app.h"

#include "attitude.h"
#include "board.h"
#include "battery.h"
#include "button.h"
#include "buzzer.h"
#include "cfg.h"
#include "chassis.h"
#include "device_profile.h"
#include "event.h"
#include "imu.h"
#include "led_scene.h"
#include "log.h"
#include "magnetometer.h"
#include "nvs.h"
#include "proto.h"

#include "bsp_uart.h"

#include "bsp_sw_qei.h"

#include "FreeRTOS.h"
#include "task.h"

/* -------------------------------------------------------------------------- */
/* 任务参数                                                                   */
/* -------------------------------------------------------------------------- */

#define APP_TASK_PRIO_EVT           (3U)
#define APP_TASK_PRIO_TMR           (4U)

#define APP_EVT_STACK_WORDS         (768U)
#define APP_TMR_STACK_WORDS         (256U)

#define APP_CTRL_PERIOD_MS          (20U)
#define APP_HEARTBEAT_PERIOD_MS     (20000U)
/** 传感器未就绪时，重试 init 的间隔（ms） */
#define APP_SENSOR_RETRY_PERIOD_MS  (1000U)
#define APP_SENSOR_RETRY_INTERVAL   (APP_SENSOR_RETRY_PERIOD_MS / APP_CTRL_PERIOD_MS)
#define APP_ATT_SAMPLE_HZ           (1000.0f / (float)APP_CTRL_PERIOD_MS)
#define APP_LED_SCENE_TICK_MS       (50U)

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

static void app_attitude_log_status_once(void)
{
    static bool_t s_logged;
    attitude_status_t status;

    if (s_logged != FALSE) {
        return;
    }
    if (attitude_get_status(&status) != STATUS_OK) {
        return;
    }
    if (status.gyro_bias_ready == FALSE) {
        return;
    }

    s_logged = TRUE;
    LOG_INFO("app:att yaw opt ready mag_trust=%u gyro_bias=1",
             (unsigned)status.mag_trust);
}

static bool_t app_sensors_boot_sample(void)
{
    imu_sample_t imu;
    magnetometer_sample_t mag;

    if ((imu_read_sample(&imu) != STATUS_OK) || (magnetometer_read_sample_fast(&mag) != STATUS_OK) ||
        (attitude_update_step(&imu, &mag) != STATUS_OK)) {
        return FALSE;
    }
    return TRUE;
}

static bool_t app_sensors_try_init(bool_t is_retry)
{
    status_t st;

    if (!device_profile_board_wants(DEVICE_BOARD_MASK_PERIPH)) {
        return FALSE;
    }
    if (imu_is_ready() && magnetometer_is_ready() && attitude_is_ready()) {
        return TRUE;
    }

    if (!imu_is_ready()) {
        st = imu_init();
        if (st != STATUS_OK) {
            if (!is_retry) {
                LOG_WARN("app:imu init failed (%d)", (int)st);
            }
            return FALSE;
        }
        LOG_INFO("app:imu ready addr=0x%02X%s",
                 (unsigned)IMU_I2C_ADDR_DEFAULT,
                 is_retry ? " (retry)" : "");
    }

    if (!magnetometer_is_ready()) {
        st = magnetometer_init();
        if (st != STATUS_OK) {
            LOG_WARN("app:mag init failed (%d)%s", (int)st, is_retry ? " (retry)" : "");
            return FALSE;
        }
        LOG_INFO("app:mag ready addr=0x%02X%s",
                 (unsigned)MAGNETOMETER_I2C_ADDR_DEFAULT,
                 is_retry ? " (retry)" : "");
    }

    if (!attitude_is_ready()) {
        if (attitude_init(APP_ATT_SAMPLE_HZ) != STATUS_OK) {
            LOG_WARN("app:att init failed%s", is_retry ? " (retry)" : "");
            return FALSE;
        }
        LOG_INFO("app:att mahony %dHz stable=accel align rotate=gyro%s",
                 (int)APP_ATT_SAMPLE_HZ,
                 is_retry ? " (retry)" : "");
        (void)app_sensors_boot_sample();
    }

    return TRUE;
}

static void app_sensors_init(void)
{
    (void)app_sensors_try_init(FALSE);
}

static void app_attitude_periodic(void)
{
    static uint32_t s_init_retry_div;

    imu_sample_t imu;
    magnetometer_sample_t mag;
    const magnetometer_sample_t *mag_ptr = NULL;

    if (!attitude_is_ready()) {
        s_init_retry_div++;
        if (s_init_retry_div >= APP_SENSOR_RETRY_INTERVAL) {
            s_init_retry_div = 0U;
            (void)app_sensors_try_init(TRUE);
        }
        return;
    }
    if (imu_read_sample(&imu) != STATUS_OK) {
        return;
    }
    if (magnetometer_read_sample_fast(&mag) == STATUS_OK) {
        mag_ptr = &mag;
    }
    if (attitude_update_step(&imu, mag_ptr) != STATUS_OK) {
        return;
    }

    app_attitude_log_status_once();
}

/* -------------------------------------------------------------------------- */

static void app_user_init(void)
{
    status_t st;

    led_scene_init();
    led_scene_run(LED_SCENE_ID_BOOTUP);

    battery_init();

    /* I2C0 软件 I2C 须在 proto_rx 与其它 vTaskDelay 之前完成，避免总线时序被打断 */
    app_sensors_init();

    if (device_profile_board_wants(DEVICE_BOARD_MASK_LINE)) {
        Line_Init();
    }

    if (device_profile_board_wants(DEVICE_BOARD_MASK_MOTOR | DEVICE_BOARD_MASK_ENCODER)) {
        chassis_init();
    }

    st = proto_uart_service_start();
    if (st != STATUS_OK) {
        LOG_WARN("app: proto service start failed (%d)", (int)st);
    }

    if (device_profile_platform_wants(DEVICE_PLATFORM_MASK_LOG)) {
        LOG_INFO("app: heap free=%u min_ever=%u",
                 (unsigned)xPortGetFreeHeapSize(),
                 (unsigned)xPortGetMinimumEverFreeHeapSize());
        LOG_INFO("app: stack hw app_evt=%u words",
                 (unsigned)uxTaskGetStackHighWaterMark(NULL));
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

    proto_telemetry_tick(APP_CTRL_PERIOD_MS);

    if (device_profile_board_wants(DEVICE_BOARD_MASK_MOTOR | DEVICE_BOARD_MASK_ENCODER)) {
        chassis_tick(APP_CTRL_PERIOD_MS);
    }
}

static void app_on_button(void)
{
    /* TODO: 按键业务 */
}

static void app_on_input(void)
{
    /* 遥控等业务事件预留；DRIVE 由 proto_telemetry_tick 在定时器上下文执行 */
}

/* -------------------------------------------------------------------------- */
/* 按键 → EVT_ID_BUTTON                                                       */
/* -------------------------------------------------------------------------- */

static void app_button_notify(btn_id_e id, const char *name, btn_permission_e permission, btn_event_e event)
{
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
        (void)log_set_level(LOG_LEVEL_INFO);
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
    static uint32_t s_led_scene_elapsed_ms;
    static bool s_stack_logged;
    uint32_t i;

    (void)arg;

    for (;;) {
        for (i = 0U; i < APP_CTRL_PERIOD_MS; i++) {
            vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(1U));
            if (device_profile_board_wants(DEVICE_BOARD_MASK_ENCODER)) {
                /* M3/M4 软件编码器：ISR 为主，1 ms 轮询备份 */
                bsp_sw_qei_poll_all();
            }
            s_led_scene_elapsed_ms++;
            if (s_led_scene_elapsed_ms >= APP_LED_SCENE_TICK_MS) {
                s_led_scene_elapsed_ms -= APP_LED_SCENE_TICK_MS;
                if (led_scene_is_active()) {
                    led_scene_update();
                }
            }
        }

        if (!s_stack_logged &&
            device_profile_platform_wants(DEVICE_PLATFORM_MASK_LOG)) {
            s_stack_logged = true;
            LOG_INFO("app: stack hw app_tmr=%u words",
                     (unsigned)uxTaskGetStackHighWaterMark(NULL));
        }

        event_set(EVT_ID_TIMER);
    }
}

/* -------------------------------------------------------------------------- */
/* 对外入口                                                                   */
/* -------------------------------------------------------------------------- */

status_t App_Start(void)
{
    status_t st = STATUS_OK;
    TaskHandle_t evt_task = NULL;
    TaskHandle_t tmr_task = NULL;

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

    buzzer_init();
    buzzer_chirp(2U, BUZZER_DEFAULT_ON_MS, BUZZER_DEFAULT_GAP_MS);

    if (xTaskCreate(app_evt_dispatch_task, APP_TASK_NAME_EVT,
                    APP_EVT_STACK_WORDS, NULL,
                    APP_TASK_PRIO_EVT, &evt_task) != pdPASS) {
        bsp_uart_debug_puts("[app] app_evt create FAIL (heap)\r\n");
        return STATUS_NO_MEM;
    }

    if (xTaskCreate(app_tmr_tick_task, APP_TASK_NAME_TMR,
                    APP_TMR_STACK_WORDS, NULL,
                    APP_TASK_PRIO_TMR, &tmr_task) != pdPASS) {
        bsp_uart_debug_puts("[app] app_tmr create FAIL (heap)\r\n");
        vTaskDelete(evt_task);
        return STATUS_NO_MEM;
    }

    (void)tmr_task;
    return STATUS_OK;
}
