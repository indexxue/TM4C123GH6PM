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
#include "device_profile.h"
#include "event.h"
#include "imu.h"
#include "led_scene.h"
#include "log.h"
#include "magnetometer.h"
#include "nvs.h"
#include "proto.h"

#include "bsp_uart.h"

#include "bsp_qei.h"
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
#define APP_ENC_LOG_PERIOD_MS       (1000U)

/** M1/M2 同速编码器排查：1=启用，0=关闭 */
#define APP_ENC_DEBUG_M1M2          (1)
/** 两路相同 RPM 符号（Motor_SetSpeed 当前为固定 50%% PWM，rpm 仅表方向） */
#define APP_ENC_DEBUG_RPM           (100)
/** 运行时长（ms），到点自动停转 */
#define APP_ENC_DEBUG_RUN_MS        (10000U)

/** 姿态解算采样率 = 1 / app_tmr 周期 */
#define APP_ATT_SAMPLE_HZ           (1000.0f / (float)APP_CTRL_PERIOD_MS)
/** 每 N 个控制周期打印一次姿态（20ms * 1000 = 20s） */
#define APP_ATT_LOG_INTERVAL        (1000U)
#define APP_LED_SCENE_TICK_MS       (50U)
/** M1/M2 排查运行中每 ms 轮询次数（M3/M4 软件编码器；M1/M2 硬件 QEI 不依赖 poll） */
#define APP_ENC_DEBUG_POLL_PER_MS   (2U)
/** 手转标定 CPR，用于 debug 日志估算圈数 */
#define APP_ENC_CPR_EST               (1450)

/* -------------------------------------------------------------------------- */
/* 心跳（在 app_evt 任务上下文采样，避免定时器回调里读 ADC）                   */
/* -------------------------------------------------------------------------- */

static uint32_t s_heartbeat_count;
static uint32_t s_heartbeat_elapsed_ms;
static uint32_t s_enc_log_elapsed_ms;

#if APP_ENC_DEBUG_M1M2
typedef enum {
    APP_ENC_DEBUG_WAIT_BOOT = 0,
    APP_ENC_DEBUG_RUNNING,
    APP_ENC_DEBUG_DONE,
} app_enc_debug_phase_e;

static app_enc_debug_phase_e s_enc_debug_phase;
static int32_t s_enc_debug_m1_start;
static int32_t s_enc_debug_m2_start;
static uint32_t s_enc_debug_elapsed_ms;

static int32_t app_enc_abs(int32_t v)
{
    return (v < 0) ? -v : v;
}

static void app_enc_debug_log_delta(int32_t d1, int32_t d2)
{
    int32_t a1 = app_enc_abs(d1);
    int32_t a2 = app_enc_abs(d2);
    long rev1_x10 = (long)((a1 * 10L) / (long)APP_ENC_CPR_EST);
    long rev2_x10 = (long)((a2 * 10L) / (long)APP_ENC_CPR_EST);

    LOG_INFO("app: enc M1M2 dM1=%ld dM2=%ld |d1|=%ld |d2|=%ld |diff|=%ld rev~%ld.%ld/%ld.%ld",
             (long)d1, (long)d2, (long)a1, (long)a2, (long)(a1 - a2),
             rev1_x10 / 10L, rev1_x10 % 10L, rev2_x10 / 10L, rev2_x10 % 10L);
}
#endif

static void app_encoder_periodic_log(void)
{
    bsp_sw_qei_poll_all();
#if APP_ENC_DEBUG_M1M2
    if (s_enc_debug_phase == APP_ENC_DEBUG_RUNNING) {
        int32_t m1 = Encoder_GetCount(0U);
        int32_t m2 = Encoder_GetCount(1U);
        int32_t d1 = m1 - s_enc_debug_m1_start;
        int32_t d2 = m2 - s_enc_debug_m2_start;

        app_enc_debug_log_delta(d1, d2);
        return;
    }
#endif
    LOG_INFO("app: enc [%ld,%ld,%ld,%ld]",
             (long)cfg_encoder_count(0U), (long)cfg_encoder_count(1U),
             (long)cfg_encoder_count(2U), (long)cfg_encoder_count(3U));
}

#if APP_ENC_DEBUG_M1M2
static void app_enc_debug_apply_motors(int32_t rpm)
{
    if (!device_profile_board_wants(DEVICE_BOARD_MASK_MOTOR)) {
        return;
    }

    Motor_SetSpeed(1U, cfg_motor_rpm(1U, rpm));
    Motor_SetSpeed(2U, cfg_motor_rpm(2U, rpm));
    Motor_SetSpeed(3U, 0);
    Motor_SetSpeed(4U, 0);
}

static void app_enc_debug_begin(void)
{
    Encoder_ResetCount(0U);
    Encoder_ResetCount(1U);
    bsp_sw_qei_poll_all();

    s_enc_debug_m1_start = Encoder_GetCount(0U);
    s_enc_debug_m2_start = Encoder_GetCount(1U);
    s_enc_debug_elapsed_ms = 0U;
    s_enc_debug_phase = APP_ENC_DEBUG_RUNNING;

    app_enc_debug_apply_motors(APP_ENC_DEBUG_RPM);
    LOG_INFO("app: enc M1M2 debug start hw-QEI qei0=%u qei1=%u rpm=%d run=%lus",
             (unsigned)bsp_qei_is_enabled(QEI0_BASE),
             (unsigned)bsp_qei_is_enabled(QEI1_BASE),
             (int)APP_ENC_DEBUG_RPM, (unsigned long)(APP_ENC_DEBUG_RUN_MS / 1000U));
}

static void app_enc_debug_arm(void)
{
    if (!device_profile_board_wants(DEVICE_BOARD_MASK_ENCODER) ||
        !device_profile_board_wants(DEVICE_BOARD_MASK_MOTOR)) {
        return;
    }

    s_enc_debug_phase = APP_ENC_DEBUG_WAIT_BOOT;
    LOG_INFO("app: enc M1M2 debug armed, wait boot LED done");
}

static void app_enc_debug_tick(void)
{
    if (s_enc_debug_phase == APP_ENC_DEBUG_DONE) {
        return;
    }

    if (s_enc_debug_phase == APP_ENC_DEBUG_WAIT_BOOT) {
        if (led_scene_is_active()) {
            return;
        }
        app_enc_debug_begin();
        return;
    }

    app_enc_debug_apply_motors(APP_ENC_DEBUG_RPM);

    s_enc_debug_elapsed_ms += APP_CTRL_PERIOD_MS;
    if (s_enc_debug_elapsed_ms >= APP_ENC_DEBUG_RUN_MS) {
        app_enc_debug_apply_motors(0);
        s_enc_debug_phase = APP_ENC_DEBUG_DONE;

        bsp_sw_qei_poll_all();
        {
            int32_t d1 = Encoder_GetCount(0U) - s_enc_debug_m1_start;
            int32_t d2 = Encoder_GetCount(1U) - s_enc_debug_m2_start;

            app_enc_debug_log_delta(d1, d2);
        }
    }
}
#endif

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

/* -------------------------------------------------------------------------- */

static void app_user_init(void)
{
    status_t st;

    led_scene_init();
    led_scene_run(LED_SCENE_ID_BOOTUP);

    battery_init();

    /* I2C0 软件 I2C 须在 proto_rx 与其它 vTaskDelay 之前完成，避免总线时序被打断 */
    app_sensors_init();

    st = proto_uart_service_start();
    if (st != STATUS_OK) {
        LOG_WARN("app: proto service start failed (%d)", (int)st);
    }

    buzzer_init();
    buzzer_chirp(2U, BUZZER_DEFAULT_ON_MS, BUZZER_DEFAULT_GAP_MS);

    if (device_profile_board_wants(DEVICE_BOARD_MASK_ENCODER)) {
        bsp_sw_qei_poll_all();
        LOG_INFO("app: enc ready [%ld,%ld,%ld,%ld] (hand-turn 1 rev per motor for CPR)",
                 (long)cfg_encoder_count(0U), (long)cfg_encoder_count(1U),
                 (long)cfg_encoder_count(2U), (long)cfg_encoder_count(3U));
#if APP_ENC_DEBUG_M1M2
        app_enc_debug_arm();
#endif
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

    if (device_profile_board_wants(DEVICE_BOARD_MASK_ENCODER)) {
        bsp_sw_qei_poll_all();
    }

    if (device_profile_board_wants(DEVICE_BOARD_MASK_ENCODER) &&
        device_profile_platform_wants(DEVICE_PLATFORM_MASK_LOG)) {
        s_enc_log_elapsed_ms += APP_CTRL_PERIOD_MS;
        if (s_enc_log_elapsed_ms >= APP_ENC_LOG_PERIOD_MS) {
            s_enc_log_elapsed_ms -= APP_ENC_LOG_PERIOD_MS;
            app_encoder_periodic_log();
        }
    }

#if APP_ENC_DEBUG_M1M2
    app_enc_debug_tick();
#endif

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
    static uint32_t s_led_scene_elapsed_ms;
    uint32_t i;

    (void)arg;

    for (;;) {
        for (i = 0U; i < APP_CTRL_PERIOD_MS; i++) {
            vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(1U));
            if (device_profile_board_wants(DEVICE_BOARD_MASK_ENCODER)) {
#if APP_ENC_DEBUG_M1M2
                uint32_t p;
                uint32_t poll_n = (s_enc_debug_phase == APP_ENC_DEBUG_RUNNING) ?
                                  APP_ENC_DEBUG_POLL_PER_MS : 2U;

                for (p = 0U; p < poll_n; p++) {
                    bsp_sw_qei_poll_all();
                }
#else
                bsp_sw_qei_poll_all();
                bsp_sw_qei_poll_all();
#endif
            }
            s_led_scene_elapsed_ms++;
            if (s_led_scene_elapsed_ms >= APP_LED_SCENE_TICK_MS) {
                s_led_scene_elapsed_ms -= APP_LED_SCENE_TICK_MS;
                if (led_scene_is_active()) {
                    led_scene_update();
                }
            }
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

    return STATUS_OK;
}
