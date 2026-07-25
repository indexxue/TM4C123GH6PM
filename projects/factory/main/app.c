/**
 * @file    app.c
 * @brief   (factory) 厂测壳：按 device_profile 门控 init；板级由构建车型 .syscfg 决定
 *
 * 构建：
 *   .\build.cmd car-4wd -Target factory   → 4 电机板 + PRODUCT_ID=1
 *   .\build.cmd car-2wd -Target factory   → 2 电机板 + PRODUCT_ID=2
 *   .\build.cmd factory                   → 兼容：factory/.syscfg（4wd 模板）+ id=0
 */

#include "app.h"

#include "attitude.h"
#include "battery.h"
#include "board.h"
#include "boot_slot.h"
#include "button.h"
#include "buzzer.h"
#include "camera_spi.h"
#include "cfg.h"
#include "cmd.h"
#include "device_profile.h"
#include "event.h"
#include "flash_layout.h"
#include "imu.h"
#include "led_scene.h"
#include "log.h"
#include "magnetometer.h"
#include "nvs.h"
#include "serial_cmd.h"

#include "bsp_i2c.h"
#include "bsp_sw_qei.h"
#include "bsp_uart.h"

#include "FreeRTOS.h"
#include "task.h"

#include <stdio.h>
#include <string.h>

#define APP_TASK_PRIO_EVT           (3U)
#define APP_TASK_PRIO_TMR           (4U)

#define APP_EVT_STACK_WORDS         (768U)
#define APP_TMR_STACK_WORDS         (256U)

#define APP_CTRL_PERIOD_MS          (20U)
#define APP_LED_SCENE_TICK_MS       (50U)
#define APP_ATT_SAMPLE_HZ           (1000.0f / (float)APP_CTRL_PERIOD_MS)
#define APP_ATT_LOG_INTERVAL        (25U)
#define APP_SENSOR_RETRY_PERIOD_MS  (1000U)
#define APP_SENSOR_RETRY_INTERVAL   (APP_SENSOR_RETRY_PERIOD_MS / APP_CTRL_PERIOD_MS)

#ifndef BOARD_MOTOR_COUNT
#define BOARD_MOTOR_COUNT 0U
#endif
#ifndef BOARD_ENCODER_COUNT
#define BOARD_ENCODER_COUNT 0U
#endif
#ifndef LINE_SENSOR_COUNT
#define LINE_SENSOR_COUNT 0U
#endif

/* -------------------------------------------------------------------------- */
/* NVS / I2C 上电摘要                                                         */
/* -------------------------------------------------------------------------- */

static void app_nvs_info_log(void)
{
    const nvs_cfg_t *cfg = nvs_cfg_get();
    const device_product_profile_t *profile = device_profile_product();
    uint32_t slot = BOOT_SLOT_A;

    (void)nvs_boot_slot_get(&slot);

    LOG_INFO("factory:nvs product=%s id=%lu serial=%s hw=%lu boot=%lu fw=%s slot=%lu first=%d",
             profile->name,
             (unsigned long)profile->product_id,
             (cfg->serial[0] != '\0') ? cfg->serial : "-",
             (unsigned long)cfg->hw_rev,
             (unsigned long)cfg->boot_count,
             (cfg->fw_version[0] != '\0') ? cfg->fw_version : "-",
             (unsigned long)slot,
             nvs_first_boot() ? 1 : 0);
}

static void app_i2c_scan_log(void)
{
    char buf[128];
    size_t len = 0U;
    int found = 0;
    uint8_t addr;

    if (!device_profile_board_wants(DEVICE_BOARD_MASK_PERIPH)) {
        return;
    }

    bsp_i2c0_gpio_scan_begin();

    for (addr = 1U; addr < 0x7FU; addr++) {
        int n;

        if (!bsp_i2c0_gpio_probe(addr)) {
            continue;
        }
        if (found > 0) {
            n = snprintf(buf + len, sizeof(buf) - len, ",0x%02X", (unsigned)addr);
        } else {
            n = snprintf(buf + len, sizeof(buf) - len, "0x%02X", (unsigned)addr);
        }
        if ((n > 0) && ((size_t)n < (sizeof(buf) - len))) {
            len += (size_t)n;
        }
        found++;
    }

    bsp_i2c0_gpio_scan_end_idle_high();
    (void)bsp_i2c_init(&BOARD_I2C_CFG);

    if (found == 0) {
        LOG_INFO("factory:i2c scan none (PB2/PB3 I2C0)");
    } else {
        LOG_INFO("factory:i2c scan %s (PB2/PB3 I2C0)", buf);
    }
}

static bool_t app_sensors_try_init(bool_t is_retry)
{
    status_t st;
    bool_t want_imu = device_profile_board_wants(DEVICE_BOARD_MASK_IMU) ? TRUE : FALSE;
    bool_t want_mag = device_profile_board_wants(DEVICE_BOARD_MASK_MAG) ? TRUE : FALSE;
    bool_t imu_ok = TRUE;
    bool_t mag_ok = TRUE;

    if ((want_imu == FALSE) && (want_mag == FALSE)) {
        return FALSE;
    }

    if ((want_imu != FALSE) && imu_is_ready() && (want_mag != FALSE) && magnetometer_is_ready() &&
        attitude_is_ready()) {
        return TRUE;
    }
    if ((want_imu != FALSE) && imu_is_ready() && (want_mag == FALSE)) {
        return TRUE;
    }

    if (want_imu != FALSE) {
        if (!imu_is_ready()) {
            st = imu_init();
            if (st != STATUS_OK) {
                if (!is_retry) {
                    LOG_WARN("factory:imu init failed (%d)", (int)st);
                }
                imu_ok = FALSE;
            } else {
                LOG_INFO("factory:imu ready addr=0x%02X%s",
                         (unsigned)IMU_I2C_ADDR_DEFAULT,
                         is_retry ? " (retry)" : "");
            }
        }
    } else {
        imu_ok = FALSE;
    }

    if (want_mag != FALSE) {
        if (!magnetometer_is_ready()) {
            st = magnetometer_init();
            if (st != STATUS_OK) {
                if (!is_retry) {
                    LOG_WARN("factory:mag init failed (%d)", (int)st);
                }
                mag_ok = FALSE;
            } else {
                LOG_INFO("factory:mag ready addr=0x%02X%s",
                         (unsigned)MAGNETOMETER_I2C_ADDR_DEFAULT,
                         is_retry ? " (retry)" : "");
            }
        }
    } else {
        mag_ok = FALSE;
    }

    if ((imu_ok != FALSE) && (mag_ok != FALSE) && !attitude_is_ready()) {
        if (attitude_init(APP_ATT_SAMPLE_HZ) != STATUS_OK) {
            if (!is_retry) {
                LOG_WARN("factory:att init failed");
            }
            return FALSE;
        }
        LOG_INFO("factory:att mahony ready %dHz 9dof%s",
                 (int)APP_ATT_SAMPLE_HZ,
                 is_retry ? " (retry)" : "");
    }

    return ((imu_ok != FALSE) || (want_imu == FALSE)) &&
           ((mag_ok != FALSE) || (want_mag == FALSE)) ? TRUE : FALSE;
}

static void app_attitude_periodic(void)
{
    static uint32_t s_init_retry_div;
    static uint32_t s_log_div;

    imu_sample_t imu;
    magnetometer_sample_t mag;
    attitude_euler_t euler;

    if (!device_profile_board_wants(DEVICE_BOARD_MASK_SENSORS)) {
        return;
    }

    if (!attitude_is_ready()) {
        s_init_retry_div++;
        if (s_init_retry_div >= APP_SENSOR_RETRY_INTERVAL) {
            s_init_retry_div = 0U;
            (void)app_sensors_try_init(TRUE);
        }
        return;
    }

    if ((imu_read_sample(&imu) != STATUS_OK) || (magnetometer_read_sample(&mag) != STATUS_OK)) {
        return;
    }
    if (attitude_update_from_sensors(&imu, &mag) != STATUS_OK) {
        return;
    }

    s_log_div++;
    if (s_log_div < APP_ATT_LOG_INTERVAL) {
        return;
    }
    s_log_div = 0U;

    if (attitude_get_euler(&euler) == STATUS_OK) {
        LOG_INFO("factory:att roll=%d pitch=%d yaw=%d deg",
                 (int)euler.roll, (int)euler.pitch, (int)euler.yaw);
    }
}

/* -------------------------------------------------------------------------- */
/* 按键：长按 OK → 回 APP_A                                                   */
/* -------------------------------------------------------------------------- */

static void app_on_button(void)
{
}

static void app_button_notify(btn_id_e id, const char *name, btn_permission_e permission,
                              btn_event_e event)
{
    if ((event == BTN_EVENT_LONG_PRESS) && ((permission & BTN_PERMISSION_FTM) != 0U)) {
        (void)boot_slot_switch(BOOT_SLOT_A);
    }
    button_log_notify(id, name, permission, event);
    event_set(EVT_ID_BUTTON);
}

/* -------------------------------------------------------------------------- */
/* app_user_init — 全部按 profile 门控                                        */
/* -------------------------------------------------------------------------- */

static void app_user_init(void)
{
    status_t st;
    const device_product_profile_t *profile = device_profile_product();

    LOG_INFO("factory: board motors=%u encoders=%u lines=%u",
             (unsigned)BOARD_MOTOR_COUNT,
             (unsigned)BOARD_ENCODER_COUNT,
             (unsigned)LINE_SENSOR_COUNT);

    if (device_profile_board_wants(DEVICE_BOARD_MASK_LED)) {
        led_scene_init();
        led_scene_run(LED_SCENE_ID_BOOTUP);
    }

    if (device_profile_board_wants(DEVICE_BOARD_MASK_BATTERY)) {
        battery_init();
    }

    app_nvs_info_log();
    app_i2c_scan_log();

    if (device_profile_board_wants(DEVICE_BOARD_MASK_SENSORS)) {
        (void)app_sensors_try_init(FALSE);
    }

    if (device_profile_platform_wants(DEVICE_PLATFORM_MASK_CMD)) {
        st = cmd_uart_line_service_start();
        if (st != STATUS_OK) {
            LOG_WARN("factory: cmd start failed (%d)", (int)st);
        } else {
            serial_cmd_register_defaults();
            LOG_INFO("factory: serial_cmd ready product=%s (help)", profile->name);
        }
    }

    if (device_profile_board_wants(DEVICE_BOARD_MASK_PERIPH)) {
        st = camera_spi_init();
        if (st != STATUS_OK) {
            LOG_WARN("factory: camera_spi init failed (%d)", (int)st);
        }
    }

    if (device_profile_platform_wants(DEVICE_PLATFORM_MASK_LOG)) {
        LOG_INFO("factory: heap free=%u min_ever=%u",
                 (unsigned)xPortGetFreeHeapSize(),
                 (unsigned)xPortGetMinimumEverFreeHeapSize());
    }
}

/* -------------------------------------------------------------------------- */
/* app_evt / app_tmr                                                          */
/* -------------------------------------------------------------------------- */

static void app_evt_dispatch_task(void *arg)
{
    (void)arg;

    if (device_profile_platform_wants(DEVICE_PLATFORM_MASK_LOG)) {
        log_notify_scheduler_running();
        (void)log_set_level(LOG_LEVEL_INFO);
    }

    if (nvs_startup_finalize() != STATUS_OK) {
        LOG_WARN("factory: nvs_startup_finalize failed");
    }

    app_user_init();
    event_set(EVT_ID_TIMER);

    for (;;) {
        event_schedule();

        if (event_is_set(EVT_ID_TIMER)) {
            if (device_profile_platform_wants(DEVICE_PLATFORM_MASK_BUTTON)) {
                button_schedule();
            }
            app_attitude_periodic();
            camera_spi_poll();
        }

        if (event_is_set(EVT_ID_BUTTON)) {
            app_on_button();
        }
    }
}

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
                bsp_sw_qei_poll_all();
            }
            if (device_profile_board_wants(DEVICE_BOARD_MASK_LED)) {
                s_led_scene_elapsed_ms++;
                if (s_led_scene_elapsed_ms >= APP_LED_SCENE_TICK_MS) {
                    s_led_scene_elapsed_ms -= APP_LED_SCENE_TICK_MS;
                    if (led_scene_is_active()) {
                        led_scene_update();
                    }
                }
            }
        }
        event_set(EVT_ID_TIMER);
    }
}

/* -------------------------------------------------------------------------- */

status_t App_Start(void)
{
    status_t st;
    TaskHandle_t evt_task = NULL;

    bsp_uart_debug_puts("[factory] nvs init\r\n");
    if (nvs_init() != STATUS_OK) {
        bsp_uart_debug_puts("[factory] nvs_init FAILED\r\n");
        return STATUS_FAIL;
    }

    cfg_init();

    st = event_init();
    if (st != STATUS_OK) {
        bsp_uart_debug_puts("[factory] event_init FAILED\r\n");
        return st;
    }

    if (device_profile_platform_wants(DEVICE_PLATFORM_MASK_BUTTON)) {
        button_init(app_button_notify);
    }

    if (device_profile_board_wants(DEVICE_BOARD_MASK_BUZZER)) {
        buzzer_init();
        buzzer_chirp(1U, BUZZER_DEFAULT_ON_MS, BUZZER_DEFAULT_GAP_MS);
    }

    bsp_uart_debug_puts("[factory] tasks start\r\n");
    if (xTaskCreate(app_evt_dispatch_task, APP_TASK_NAME_EVT,
                    APP_EVT_STACK_WORDS, NULL,
                    APP_TASK_PRIO_EVT, &evt_task) != pdPASS) {
        bsp_uart_debug_puts("[factory] app_evt create FAIL\r\n");
        return STATUS_NO_MEM;
    }

    if (xTaskCreate(app_tmr_tick_task, APP_TASK_NAME_TMR,
                    APP_TMR_STACK_WORDS, NULL,
                    APP_TASK_PRIO_TMR, NULL) != pdPASS) {
        bsp_uart_debug_puts("[factory] app_tmr create FAIL\r\n");
        vTaskDelete(evt_task);
        return STATUS_NO_MEM;
    }

    bsp_uart_debug_puts("[factory] rtos start\r\n");
    return STATUS_OK;
}
