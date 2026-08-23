/**
 * @file    app.c
 * @brief   遥控器：操纵杆 → 蓝牙 DRIVE；IMU/磁力计姿态；SPI 外设占位
 */

#include "app.h"

#include "joystick.h"
#include "lcd_panel.h"
#include "nrf24.h"

#include "attitude.h"
#include "battery.h"
#include "board.h"
#include "device_profile.h"
#include "event.h"
#include "imu.h"
#include "led_scene.h"
#include "log.h"
#include "magnetometer.h"
#include "proto_client.h"

#include "bsp_adc.h"
#include "bsp_i2c.h"
#include "bsp_spi.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "driverlib/gpio.h"

#include "FreeRTOS.h"
#include "task.h"

/** 上电外设冒烟：验证通过后改 0 或删除下方 #if RC_BOOT_SMOKE 整块 */
#define RC_BOOT_SMOKE               1

#define RC_TASK_PRIO_EVT            (3U)
#define RC_TASK_PRIO_TMR            (4U)

#define RC_EVT_STACK_WORDS          (1024U)
#define RC_TMR_STACK_WORDS          (256U)

#define RC_CTRL_PERIOD_MS           (20U)
#define RC_LED_SCENE_TICK_MS        (50U)
#define RC_SENSOR_RETRY_MS          (1000U)
#define RC_ATT_SAMPLE_HZ            (50.0f)

static TaskHandle_t s_evt_task;
static TaskHandle_t s_tmr_task;

static bool_t s_imu_ready;
static bool_t s_mag_ready;
static uint16_t s_sensor_retry_ms;
static uint16_t s_ui_ms;
static int16_t s_last_throttle;
static int16_t s_last_steer;

static void rc_peripherals_init(void)
{
    if (device_profile_board_wants(DEVICE_BOARD_MASK_JOYSTICK)) {
        if (board_joystick_init() != STATUS_OK) {
            LOG_WARN("rc: joystick adc init fail");
        }
    }

    if (device_profile_board_wants(DEVICE_BOARD_MASK_NRF24)) {
        board_nrf24_gpio_init();
        LOG_INFO("rc: nrf gpio ready (driver TBD)");
    }
}

#if RC_BOOT_SMOKE

#define RC_SMOKE_BAT_DIVIDER_NUM    2U

static void rc_smoke_i2c_scan(char *buf, size_t buf_len)
{
    size_t len = 0U;
    int found = 0;
    uint8_t addr;

    if ((buf == NULL) || (buf_len == 0U)) {
        return;
    }
    buf[0] = '\0';

    bsp_i2c0_gpio_scan_begin();
    for (addr = 1U; addr < 0x7FU; addr++) {
        int n;

        if (!bsp_i2c0_gpio_probe(addr)) {
            continue;
        }
        if (found > 0) {
            n = snprintf(buf + len, buf_len - len, ",0x%02X", (unsigned)addr);
        } else {
            n = snprintf(buf + len, buf_len - len, "0x%02X", (unsigned)addr);
        }
        if ((n > 0) && ((size_t)n < (buf_len - len))) {
            len += (size_t)n;
        }
        found++;
    }
    bsp_i2c0_gpio_scan_end_idle_high();
    (void)bsp_i2c_init(&BOARD_I2C_CFG);

    if (found == 0) {
        (void)snprintf(buf, buf_len, "none");
    }
}

static void rc_boot_smoke(void)
{
    char i2c_buf[64];
    uint16_t j1x = 0U;
    uint16_t j1y = 0U;
    uint32_t vbatt_mv = 0U;
    uint8_t pass = 0U;
    uint8_t total = 0U;

    LOG_INFO("smoke: start");

    total++;
    rc_smoke_i2c_scan(i2c_buf, sizeof(i2c_buf));
    LOG_INFO("smoke: i2c %s", i2c_buf);
    if ((strstr(i2c_buf, "0x69") != NULL) && (strstr(i2c_buf, "0x2C") != NULL)) {
        pass++;
    } else {
        LOG_WARN("smoke: i2c expect 0x69,0x2C");
    }

    total++;
    {
        imu_sample_t sample;

        if ((imu_is_ready() || (imu_init() == STATUS_OK)) && (imu_read_sample(&sample) == STATUS_OK)) {
            pass++;
            LOG_INFO("smoke: imu ok");
        } else {
            LOG_WARN("smoke: imu fail");
        }
    }

    total++;
    {
        magnetometer_sample_t sample;

        if ((magnetometer_is_ready() || (magnetometer_init() == STATUS_OK)) &&
            (magnetometer_read_sample(&sample) == STATUS_OK)) {
            pass++;
            LOG_INFO("smoke: mag ok");
        } else {
            LOG_WARN("smoke: mag fail");
        }
    }

    total++;
    {
        uint32_t raw[4];

        if (bsp_adc_sample(&BOARD_JOY_ADC_CFG, raw, 4U)) {
            j1x = (uint16_t)(raw[0] & 0xFFFFU);
            j1y = (uint16_t)(raw[1] & 0xFFFFU);
            (void)GPIOPinRead(GPIO_JS1_BTN_PORT, GPIO_JS1_BTN_MASK);
            (void)GPIOPinRead(GPIO_JS2_BTN_PORT, GPIO_JS2_BTN_MASK);
            pass++;
            LOG_INFO("smoke: joy j1=%u,%u", (unsigned)j1x, (unsigned)j1y);
        } else {
            LOG_WARN("smoke: joy fail");
        }
    }

    total++;
    {
        battery_voltage_t v;
        uint32_t raw = 0U;
        bool bat_ok = false;

        battery_init();
        if (battery_percent_update() && (battery_voltage_read_mv(&v) > 0U)) {
            vbatt_mv = v.current_mv;
            bat_ok = true;
        } else if (bsp_adc_sample_one(&BOARD_BATTERY_ADC_CFG, &raw)) {
            vbatt_mv = ((raw * 3300U) / 4095U) * RC_SMOKE_BAT_DIVIDER_NUM;
            bat_ok = true;
        }
        if (bat_ok) {
            pass++;
            LOG_INFO("smoke: bat %lumV", (unsigned long)vbatt_mv);
        } else {
            LOG_WARN("smoke: bat fail");
        }
    }

    total++;
    led_scene_rgb_set(0U, 32U, 0U);
    pass++;
    LOG_INFO("smoke: led green");

    total++;
    {
        uint32_t irq = GPIOPinRead(GPIO_NRF_IRQ_PORT, GPIO_NRF_IRQ_MASK);
        uint32_t ce = GPIOPinRead(GPIO_NRF_CE_PORT, GPIO_NRF_CE_MASK);
        uint32_t cs = GPIOPinRead(GPIO_NRF_CS_PORT, GPIO_NRF_CS_MASK);
        uint8_t tx = 0x55U;
        uint8_t rx = 0U;
        const bsp_spi_cs_t spi_cs = {
            .cs_pin = &(const bsp_gpio_pin_t){GPIO_NRF_CS_PORT, GPIO_NRF_CS_MASK},
            .cs_active_low = true,
        };

        LOG_INFO("smoke: nrf irq=%lu ce=%lu cs=%lu",
                 (unsigned long)((irq != 0U) ? 1U : 0U),
                 (unsigned long)((ce != 0U) ? 1U : 0U),
                 (unsigned long)((cs != 0U) ? 1U : 0U));
        LOG_INFO("smoke: lcd cs=%lu bl=%lu",
                 (unsigned long)((GPIOPinRead(GPIO_LCD_CS_PORT, GPIO_LCD_CS_MASK) != 0U) ? 1U : 0U),
                 (unsigned long)((GPIOPinRead(GPIO_LCD_BL_PORT, GPIO_LCD_BL_MASK) != 0U) ? 1U : 0U));

        if (bsp_spi_init(&BOARD_SPI_CFG) &&
            bsp_spi_transceive(BOARD_SPI_CFG.base, &spi_cs, &tx, &rx, 1U)) {
            pass++;
            LOG_INFO("smoke: spi ok");
        } else {
            LOG_WARN("smoke: spi fail");
        }
    }

    total++;
    if (proto_client_link_up()) {
        pass++;
        LOG_INFO("smoke: proto link up");
    } else if (proto_client_send_hello() == STATUS_OK) {
        pass++;
        LOG_INFO("smoke: proto hello sent");
    } else {
        LOG_WARN("smoke: proto fail");
    }

    LOG_INFO("smoke: done pass=%u/%u", (unsigned)pass, (unsigned)total);
}

#endif /* RC_BOOT_SMOKE */

static void rc_sensors_try_init(void)
{
    if (device_profile_board_wants(DEVICE_BOARD_MASK_IMU) && !s_imu_ready) {
        s_imu_ready = (imu_init() == STATUS_OK) ? TRUE : FALSE;
        if (s_imu_ready) {
            LOG_INFO("rc: imu ready");
        }
    }
    if (device_profile_board_wants(DEVICE_BOARD_MASK_MAG) && !s_mag_ready) {
        s_mag_ready = (magnetometer_init() == STATUS_OK) ? TRUE : FALSE;
        if (s_mag_ready) {
            LOG_INFO("rc: mag ready");
        }
    }
    if (s_imu_ready && s_mag_ready) {
        (void)attitude_init(RC_ATT_SAMPLE_HZ);
    }
}

static void rc_on_timer(void)
{
    board_joystick_sample_t js[BOARD_JOYSTICK_COUNT];

    proto_client_tick(RC_CTRL_PERIOD_MS);

    if (device_profile_board_wants(DEVICE_BOARD_MASK_JOYSTICK)) {
        if (board_joystick_sample(js)) {
            s_last_throttle = board_joystick_axis_to_cmd(js[0].y);
            s_last_steer = board_joystick_axis_to_cmd(js[0].x);
            (void)proto_client_send_drive(s_last_throttle, s_last_steer);
        }
    }

    if (lcd_panel_is_ready()) {
        if (s_ui_ms < 200U) {
            s_ui_ms = (uint16_t)(s_ui_ms + RC_CTRL_PERIOD_MS);
        } else {
            battery_voltage_t v;
            uint32_t bat_mv = 0U;

            s_ui_ms = 0U;
            if (battery_voltage_read_mv(&v) > 0) {
                bat_mv = v.current_mv;
            }
            lcd_panel_update_telemetry(s_last_throttle, s_last_steer, bat_mv, proto_client_link_up());
        }
    }

    if (s_sensor_retry_ms < RC_SENSOR_RETRY_MS) {
        s_sensor_retry_ms = (uint16_t)(s_sensor_retry_ms + RC_CTRL_PERIOD_MS);
    } else {
        s_sensor_retry_ms = 0U;
        rc_sensors_try_init();
    }
}

static void rc_evt_task_fn(void *arg)
{
    (void)arg;

    log_notify_scheduler_running();
    (void)log_set_level(LOG_LEVEL_INFO);

    rc_peripherals_init();
#if RC_BOOT_SMOKE
    rc_boot_smoke();
#endif

    if (device_profile_board_wants(DEVICE_BOARD_MASK_LCD)) {
        if (lcd_panel_init() == STATUS_OK) {
            lcd_panel_show_boot();
        }
    }

    rc_sensors_try_init();

    if (device_profile_board_wants(DEVICE_BOARD_MASK_BATTERY)) {
        battery_init();
    }

    if (device_profile_board_wants(DEVICE_BOARD_MASK_LED)) {
        led_scene_init();
        led_scene_run(LED_SCENE_ID_BOOTUP);
    }

    event_set(EVT_ID_TIMER);

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

    (void)proto_client_init();
    (void)proto_client_send_hello();

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
