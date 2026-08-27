/**
 * @file    app.c
 * @brief   遥控器应用：摇杆校准/菜单 UI + 蓝牙 DRIVE（须 JS1 短按进会话）
 */

#include "app.h"

#include "joy_cal.h"
#include "joystick.h"
#include "lcd_panel.h"
#include "nrf24.h"
#include "rc_link.h"
#include "rc_model.h"
#include "rc_target.h"
#include "rc_sub.h"
#include "rc_mixer.h"
#include "rc_ui.h"

#include "attitude.h"
#include "battery.h"
#include "board.h"
#include "cfg.h"
#include "device_profile.h"
#include "event.h"
#include "flash_layout.h"
#include "imu.h"
#include "led_scene.h"
#include "log.h"
#include "nvs.h"
#include "proto_client.h"

#include "FreeRTOS.h"
#include "task.h"

/** 磁力计未焊接时置 0 */
#define RC_MAG_ENABLE               0

#if RC_MAG_ENABLE
#include "magnetometer.h"
#endif

#define RC_TASK_PRIO_EVT            (3U)
#define RC_TASK_PRIO_TMR            (4U)

#define RC_EVT_STACK_WORDS          (1280U)
#define RC_TMR_STACK_WORDS          (256U)

#define RC_CTRL_PERIOD_MS           (20U)
#define RC_LED_SCENE_TICK_MS        (50U)
#define RC_SENSOR_RETRY_MS          1000U
#define RC_ATT_SAMPLE_HZ            (50.0f)
/** UART7 通信诊断：LINK 后每 3s 一行 rc:stats（找半双工平衡点） */
#define RC_PROTO_STATS_LOG_MS       3000U

static TaskHandle_t s_evt_task;
static TaskHandle_t s_tmr_task;

static bool_t s_imu_ready;
#if RC_MAG_ENABLE
static bool_t s_mag_ready;
#endif
static uint16_t s_sensor_retry_ms;
static uint32_t s_stats_log_ms;

static const char *rc_ui_mode_tag(void)
{
    switch (rc_ui_mode()) {
    case RC_UI_MODE_DRIVE:
        return "DRIVE";
    case RC_UI_MODE_SUBSCRIBE:
        return "SUB";
    case RC_UI_MODE_MENU:
        return "MENU";
    case RC_UI_MODE_CAL:
        return "CAL";
    default:
        return "IDLE";
    }
}

static void rc_log_device_info(void)
{
    const nvs_cfg_t *cfg = nvs_cfg_get();
    const device_product_profile_t *profile = device_profile_product();
    uint32_t slot = BOOT_SLOT_A;

    (void)nvs_boot_slot_get(&slot);

    LOG_INFO("rc:nvs product=%s id=%lu serial=%s hw=%lu boot=%lu fw=%s slot=%lu first=%d",
             profile->name,
             (unsigned long)profile->product_id,
             (cfg->serial[0] != '\0') ? cfg->serial : "-",
             (unsigned long)cfg->hw_rev,
             (unsigned long)cfg->boot_count,
             (cfg->fw_version[0] != '\0') ? cfg->fw_version : "-",
             (unsigned long)slot,
             nvs_first_boot() ? 1 : 0);
}

static void rc_peripherals_init(void)
{
    if (device_profile_board_wants(DEVICE_BOARD_MASK_JOYSTICK)) {
        if (board_joystick_init() != STATUS_OK) {
            LOG_WARN("rc: joystick adc init fail");
        }
    }

    if (joy_cal_init() != STATUS_OK) {
        LOG_WARN("rc: joy_cal init fail");
    }
    if (rc_sub_init() != STATUS_OK) {
        LOG_WARN("rc: sub mask init fail");
    }
    if (rc_target_init() != STATUS_OK) {
        LOG_WARN("rc: target init fail");
    }
    if (rc_model_init() != STATUS_OK) {
        LOG_WARN("rc: model init fail");
    }
    if (rc_mixer_init() != STATUS_OK) {
        LOG_WARN("rc: mixer init fail");
    }

    if (device_profile_board_wants(DEVICE_BOARD_MASK_NRF24)) {
        board_nrf24_gpio_init();
        LOG_INFO("rc: nrf gpio ready (driver TBD)");
    }
}

static bool_t rc_app_tilt_active(void)
{
    const rc_model_t *model = rc_model_active();

    if ((model == NULL) || (model->input_src != RC_MODEL_INPUT_IMU_TILT)) {
        return FALSE;
    }
    switch (rc_ui_mode()) {
    case RC_UI_MODE_HOME:
    case RC_UI_MODE_DRIVE:
        return TRUE;
    default:
        return FALSE;
    }
}

static void rc_sensors_try_init(void)
{
    if (device_profile_board_wants(DEVICE_BOARD_MASK_IMU) && !s_imu_ready) {
        s_imu_ready = (imu_init() == STATUS_OK) ? TRUE : FALSE;
        if (s_imu_ready) {
            LOG_INFO("rc: imu ready");
        }
    }
#if RC_MAG_ENABLE
    if (device_profile_board_wants(DEVICE_BOARD_MASK_MAG) && !s_mag_ready) {
        s_mag_ready = (magnetometer_init() == STATUS_OK) ? TRUE : FALSE;
        if (s_mag_ready) {
            LOG_INFO("rc: mag ready");
        }
    }
    if (s_imu_ready && s_mag_ready) {
        (void)attitude_init(RC_ATT_SAMPLE_HZ);
    }
#else
    if (s_imu_ready && (attitude_is_ready() == FALSE)) {
        if (attitude_init(RC_ATT_SAMPLE_HZ) == STATUS_OK) {
            LOG_INFO("rc: attitude 6-dof ready");
        }
    }
#endif
}

static void rc_on_timer(void)
{
    rc_link_tick(RC_CTRL_PERIOD_MS);

    rc_mixer_tick(RC_CTRL_PERIOD_MS, rc_app_tilt_active());

    rc_ui_tick(RC_CTRL_PERIOD_MS);

    if (device_profile_board_wants(DEVICE_BOARD_MASK_JOYSTICK)) {
        rc_link_drive_update(rc_ui_last_throttle(), rc_ui_last_steer(), rc_ui_drive_muted());
    }

    if (s_sensor_retry_ms < RC_SENSOR_RETRY_MS) {
        s_sensor_retry_ms = (uint16_t)(s_sensor_retry_ms + RC_CTRL_PERIOD_MS);
    } else {
        s_sensor_retry_ms = 0U;
        rc_sensors_try_init();
    }

    if (rc_link_up() != FALSE) {
        s_stats_log_ms += RC_CTRL_PERIOD_MS;
        if (s_stats_log_ms >= RC_PROTO_STATS_LOG_MS) {
            s_stats_log_ms = 0U;
            proto_client_stats_log_delta(rc_ui_mode_tag(), (uint32_t)xPortGetFreeHeapSize(),
                                         (uint32_t)uxTaskGetStackHighWaterMark(s_evt_task));
        }
    } else {
        s_stats_log_ms = 0U;
    }
}

static void rc_evt_task_fn(void *arg)
{
    (void)arg;

    log_notify_scheduler_running();
    (void)log_set_level(LOG_LEVEL_INFO);

    if (nvs_init() != STATUS_OK) {
        LOG_WARN("rc: nvs_init fail (cal will use defaults)");
    }
    if (nvs_startup_finalize() != STATUS_OK) {
        LOG_WARN("rc: nvs_startup_finalize fail");
    }
    cfg_init();
    rc_log_device_info();

    rc_peripherals_init();

    if (device_profile_board_wants(DEVICE_BOARD_MASK_LCD)) {
        if (lcd_panel_init() == STATUS_OK) {
            (void)rc_ui_init();
        } else {
            LOG_WARN("rc: lcd init fail");
        }
    }

    rc_sensors_try_init();

    if (device_profile_board_wants(DEVICE_BOARD_MASK_BATTERY)) {
        battery_init();
        {
            uint8_t pct = battery_get_percent();
            battery_voltage_t bat;

            if ((pct != BATTERY_PERCENT_UNKNOWN) &&
                (battery_voltage_read_mv(&bat) > 0U)) {
                LOG_INFO("rc: battery %lumV %u%%",
                         (unsigned long)bat.current_mv, (unsigned)pct);
            } else {
                LOG_WARN("rc: battery read fail");
            }
        }
    }

    if (device_profile_board_wants(DEVICE_BOARD_MASK_LED)) {
        led_scene_init();
        led_scene_run(LED_SCENE_ID_BOOTUP);
    }

    event_set(EVT_ID_TIMER);
    LOG_INFO("rc: ui ready mode=HOME (JS1 connect, JS2 target)");

    for (;;) {
        event_schedule();

        if (event_is_set(EVT_ID_TIMER)) {
            rc_on_timer();
        }
    }
}

static void rc_tmr_task_fn(void *arg)
{
    uint32_t led_ms = 0U;

    (void)arg;

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(RC_CTRL_PERIOD_MS));
        event_set(EVT_ID_TIMER);

        led_ms += RC_CTRL_PERIOD_MS;
        if (led_ms >= RC_LED_SCENE_TICK_MS) {
            led_ms = 0U;
            if (device_profile_board_wants(DEVICE_BOARD_MASK_LED) && led_scene_is_active()) {
                led_scene_update();
            }
        }
    }
}

status_t App_Start(void)
{
    if (event_init() != STATUS_OK) {
        LOG_ERROR("rc: event_init fail");
        return STATUS_FAIL;
    }

    (void)rc_link_init();

    if (xTaskCreate(rc_evt_task_fn, APP_TASK_NAME_EVT, RC_EVT_STACK_WORDS, NULL, RC_TASK_PRIO_EVT,
                    &s_evt_task) != pdPASS) {
        LOG_ERROR("rc: evt task create fail");
        return STATUS_FAIL;
    }
    if (xTaskCreate(rc_tmr_task_fn, APP_TASK_NAME_TMR, RC_TMR_STACK_WORDS, NULL, RC_TASK_PRIO_TMR,
                    &s_tmr_task) != pdPASS) {
        LOG_ERROR("rc: tmr task create fail");
        return STATUS_FAIL;
    }

    LOG_INFO("rc: tasks started");
    return STATUS_OK;
}
