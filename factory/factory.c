/**
 * @file    factory.c
 * @brief   厂测固件：factory_evt 事件调度 + factory_tmr 周期定时 + cmd 接口
 */

#include "factory.h"

#include "board.h"
#include "bsp_i2c.h"
#include "bsp_sysctl.h"
#include "bsp_systick.h"
#include "bsp_uart.h"
#include "button.h"
#include "cmd.h"
#include "event.h"
#include "flash_layout.h"
#include "imu.h"
#include "log.h"
#include "magnetometer.h"
#include "attitude.h"
#include "nvs.h"

#include "inc/hw_memmap.h"

#include "FreeRTOS.h"
#include "task.h"

#include <stdio.h>
#include <string.h>

#define FACTORY_VERSION             "ft-0.1.0"

#define FACTORY_TASK_PRIO_EVT       (3U)
#define FACTORY_TASK_PRIO_TMR       (2U)

#define FACTORY_EVT_STACK_WORDS     (768U)
#define FACTORY_TMR_STACK_WORDS     (256U)

#define FACTORY_CTRL_PERIOD_MS      (20U)

/** 姿态解算采样率 = 1 / factory_tmr 周期 */
#define FACTORY_ATT_SAMPLE_HZ       (1000.0f / (float)FACTORY_CTRL_PERIOD_MS)
/** 每 N 个控制周期打印一次姿态（20ms * 25 = 500ms） */
#define FACTORY_ATT_LOG_INTERVAL    (25U)

/* -------------------------------------------------------------------------- */
/* 上电 NVS 摘要（仅打印一次）                                                 */
/* -------------------------------------------------------------------------- */

static void factory_nvs_info_log(void)
{
    const nvs_cfg_t *cfg = nvs_cfg_get();
    uint32_t slot = BOOT_SLOT_A;

    (void)nvs_boot_slot_get(&slot);

    LOG_INFO("factory:nvs serial=%s hw=%lu boot=%lu fw=%s slot=%lu first=%d",
             (cfg->serial[0] != '\0') ? cfg->serial : "-",
             (unsigned long)cfg->hw_rev,
             (unsigned long)cfg->boot_count,
             (cfg->fw_version[0] != '\0') ? cfg->fw_version : "-",
             (unsigned long)slot,
             nvs_first_boot() ? 1 : 0);
}

/* -------------------------------------------------------------------------- */
/* 上电 I2C 地址扫描（factory_evt 内直接测，不经 cmd）                         */
/* -------------------------------------------------------------------------- */

static void factory_i2c_scan_log(void)
{
    char buf[128];
    size_t len = 0U;
    int found = 0;

    bsp_i2c0_gpio_scan_begin();

    for (uint8_t addr = 1U; addr < 0x7FU; addr++) {
        if (!bsp_i2c0_gpio_probe(addr)) {
            continue;
        }

        {
            int n;

            if (found > 0) {
                n = snprintf(buf + len, sizeof(buf) - len, ",0x%02X", (unsigned int)addr);
            } else {
                n = snprintf(buf + len, sizeof(buf) - len, "0x%02X", (unsigned int)addr);
            }
            if ((n > 0) && ((size_t)n < (sizeof(buf) - len))) {
                len += (size_t)n;
            }
            found++;
        }
    }

    bsp_i2c0_gpio_scan_end_idle_high();

    {
        bool scl_high = false;
        bool sda_high = false;

        (void)bsp_i2c0_sample_idle_lines(&scl_high, &sda_high);
        LOG_INFO("factory:i2c idle scl=%u sda=%u (1=high)",
                 (unsigned)scl_high, (unsigned)sda_high);
    }

    if (found == 0) {
        LOG_INFO("factory:i2c scan none (PB2/PB3 I2C0)");
    } else {
        LOG_INFO("factory:i2c scan %s (PB2/PB3 I2C0)", buf);
    }

    (void)bsp_i2c_init(&BOARD_I2C_CFG);
}

static void factory_i2c_chip_probe_log(void)
{
    uint8_t id = 0u;

    if (bsp_i2c_read_byte(I2C0_BASE, IMU_I2C_ADDR_DEFAULT, 0x75u, &id)) {
        LOG_INFO("factory:probe imu@0x%02X reg0x75=0x%02X", (unsigned)IMU_I2C_ADDR_DEFAULT, (unsigned)id);
    } else {
        LOG_WARN("factory:probe imu@0x%02X reg0x75 read fail", (unsigned)IMU_I2C_ADDR_DEFAULT);
    }

    if (bsp_i2c_read_byte(I2C0_BASE, MAGNETOMETER_I2C_ADDR_DEFAULT, 0x00u, &id)) {
        LOG_INFO("factory:probe mag@0x%02X reg0x00=0x%02X",
                 (unsigned)MAGNETOMETER_I2C_ADDR_DEFAULT, (unsigned)id);
    } else {
        LOG_WARN("factory:probe mag@0x%02X reg0x00 read fail", (unsigned)MAGNETOMETER_I2C_ADDR_DEFAULT);
    }
}

/* -------------------------------------------------------------------------- */
/* IMU / 磁力计厂测（I2C0 软件 I2C，地址见 imu.h / magnetometer.h）           */
/* -------------------------------------------------------------------------- */

static void factory_sensors_test_log(void)
{
    imu_sample_t imu;
    magnetometer_sample_t mag;
    int16_t temp_c = 0;
    status_t st;
    bool_t imu_ok = FALSE;
    bool_t mag_ok = FALSE;

    st = imu_init();
    if (st != STATUS_OK) {
        LOG_WARN("factory:imu init failed (%d)", (int)st);
    } else {
        imu_ok = TRUE;
        LOG_INFO("factory:imu ready addr=0x%02X", (unsigned)IMU_I2C_ADDR_DEFAULT);
        st = imu_read_sample(&imu);
        if (st == STATUS_OK) {
            LOG_INFO("factory:imu ax=%d ay=%d az=%d gx=%d gy=%d gz=%d",
                     (int)imu.ax, (int)imu.ay, (int)imu.az,
                     (int)imu.gx, (int)imu.gy, (int)imu.gz);
        } else {
            LOG_WARN("factory:imu read failed (%d)", (int)st);
        }

        if (imu_read_temperature(&temp_c) == STATUS_OK) {
            LOG_INFO("factory:imu temp=%dC", (int)temp_c);
        }
    }

    st = magnetometer_init();
    if (st != STATUS_OK) {
        LOG_WARN("factory:mag init failed (%d)", (int)st);
    } else {
        mag_ok = TRUE;
        LOG_INFO("factory:mag ready addr=0x%02X", (unsigned)MAGNETOMETER_I2C_ADDR_DEFAULT);
        st = magnetometer_read_sample(&mag);
        if (st == STATUS_OK) {
            LOG_INFO("factory:mag mx=%d my=%d mz=%d",
                     (int)mag.mx, (int)mag.my, (int)mag.mz);
        } else {
            LOG_WARN("factory:mag read failed (%d)", (int)st);
        }
    }

    if ((imu_ok != FALSE) && (mag_ok != FALSE)) {
        if (attitude_init(FACTORY_ATT_SAMPLE_HZ) == STATUS_OK) {
            LOG_INFO("factory:att madgwick ready %dHz 9dof", (int)FACTORY_ATT_SAMPLE_HZ);
            if ((imu_read_sample(&imu) == STATUS_OK) && (magnetometer_read_sample(&mag) == STATUS_OK) &&
                (attitude_update_from_sensors(&imu, &mag) == STATUS_OK)) {
                attitude_euler_t euler;

                if (attitude_get_euler(&euler) == STATUS_OK) {
                    LOG_INFO("factory:att roll=%d pitch=%d yaw=%d (0.1deg)",
                             (int)euler.roll_x10, (int)euler.pitch_x10, (int)euler.yaw_x10);
                }
            }
        } else {
            LOG_WARN("factory:att init failed");
        }
    }
}

/* -------------------------------------------------------------------------- */
/* 周期姿态更新（factory_tmr 驱动）                                            */
/* -------------------------------------------------------------------------- */

static void factory_attitude_periodic(void)
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

    s_log_div++;
    if (s_log_div < FACTORY_ATT_LOG_INTERVAL) {
        return;
    }
    s_log_div = 0U;

    if (attitude_get_euler(&euler) == STATUS_OK) {
        LOG_INFO("factory:att roll=%d pitch=%d yaw=%d (0.1deg)",
                 (int)euler.roll_x10, (int)euler.pitch_x10, (int)euler.yaw_x10);
    }
}

/* -------------------------------------------------------------------------- */
/* cmd 接口                                                                   */
/* -------------------------------------------------------------------------- */

static void factory_cmd_ftm(int argc, const char *argv[])
{
    char buf[CMD_STATUS_BUF_SIZE];

    if ((argc >= 2) && (strcmp(argv[1], "version") == 0)) {
        cmd_reply_ok("ftm", FACTORY_VERSION);
        return;
    }

    (void)snprintf(buf, sizeof(buf), "version=%s base=0x%08lX",
                   FACTORY_VERSION, (unsigned long)FLASH_APP_B_BASE);
    cmd_reply_ok("ftm", buf);
}

static void factory_cmd_att(int argc, const char *argv[])
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

static void factory_cmd_register(void)
{
    (void)cmd_register("ftm", factory_cmd_ftm, "ftm [version] factory status");
    (void)cmd_register("att", factory_cmd_att, "att read euler angles (0.1deg)");
}

/* -------------------------------------------------------------------------- */
/* 业务钩子                                                                   */
/* -------------------------------------------------------------------------- */

static void factory_on_button(void)
{
    /* 预留：厂测按键业务 */
}

/* -------------------------------------------------------------------------- */
/* 按键 → EVT_ID_BUTTON                                                       */
/* -------------------------------------------------------------------------- */

static void factory_button_notify(btn_id_e id, const char *name, btn_permission_e permission,
                                  btn_event_e event)
{
    if ((event == BTN_EVENT_LONG_PRESS) && ((permission & BTN_PERMISSION_FTM) != 0U)) {
        (void)cmd_boot_slot_switch(BOOT_SLOT_A);
    }
    button_log_notify(id, name, permission, event);
    event_set(EVT_ID_BUTTON);
}

/* -------------------------------------------------------------------------- */
/* factory_evt：阻塞 event_schedule，按位分发                                 */
/* -------------------------------------------------------------------------- */

static void factory_evt_dispatch_task(void *arg)
{
    (void)arg;

    log_notify_scheduler_running();
    (void)log_set_level(LOG_LEVEL_INFO);

    if (nvs_startup_finalize() != STATUS_OK) {
        LOG_WARN("factory: nvs_startup_finalize failed");
    }

    factory_nvs_info_log();
    factory_i2c_scan_log();
    factory_i2c_chip_probe_log();
    factory_sensors_test_log();

    event_set(EVT_ID_TIMER);

    for (;;) {
        event_schedule();

        if (event_is_set(EVT_ID_TIMER)) {
            button_schedule();
            factory_attitude_periodic();
        }

        if (event_is_set(EVT_ID_BUTTON)) {
            factory_on_button();
        }
    }
}

/* -------------------------------------------------------------------------- */
/* factory_tmr：周期投递 EVT_ID_TIMER                                         */
/* -------------------------------------------------------------------------- */

static void factory_tmr_tick_task(void *arg)
{
    TickType_t last_wake = xTaskGetTickCount();

    (void)arg;

    for (;;) {
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(FACTORY_CTRL_PERIOD_MS));
        event_set(EVT_ID_TIMER);
    }
}

/* -------------------------------------------------------------------------- */
/* 对外入口                                                                   */
/* -------------------------------------------------------------------------- */

void Factory_Board_Init(void)
{
    bsp_clock_init(BSP_CLOCK_MAIN_8MHZ);
    bsp_systick_init();
    (void)Board_UartDebug_Init();
    bsp_uart_debug_puts("[factory] boot\r\n");

    /* 厂测首阶段：UART7 日志 + PB2/PB3 软件 I2C；勿走 Board_Periph_Init（含未时钟 I2C0 寄存器访问） */
    bsp_uart_debug_puts("[factory] i2c sw init\r\n");
    if (!bsp_gpio_port_enable(0x02u)) {
        bsp_uart_debug_puts("[factory] gpioB FAILED\r\n");
    } else if (!bsp_i2c_init(&BOARD_I2C_CFG)) {
        bsp_uart_debug_puts("[factory] i2c FAILED\r\n");
    } else {
        bsp_uart_debug_puts("[factory] board ok\r\n");
    }

    (void)log_init(NULL);
    bsp_uart_debug_puts("[factory] board init done\r\n");
}

status_t Factory_Start(void)
{
    status_t st;

    bsp_uart_debug_puts("[factory] nvs init\r\n");
    if (nvs_init() != STATUS_OK) {
        bsp_uart_debug_puts("[factory] nvs_init FAILED\r\n");
        return STATUS_FAIL;
    }

    st = event_init();
    if (st != STATUS_OK) {
        bsp_uart_debug_puts("[factory] event_init FAILED\r\n");
        return st;
    }

    button_init(factory_button_notify);

    bsp_uart_debug_puts("[factory] tasks start\r\n");
    if (xTaskCreate(factory_evt_dispatch_task, FACTORY_TASK_NAME_EVT,
                    FACTORY_EVT_STACK_WORDS, NULL,
                    FACTORY_TASK_PRIO_EVT, NULL) != pdPASS) {
        bsp_uart_debug_puts("[factory] factory_evt create FAIL (heap)\r\n");
        return STATUS_NO_MEM;
    }

    if (xTaskCreate(factory_tmr_tick_task, FACTORY_TASK_NAME_TMR,
                    FACTORY_TMR_STACK_WORDS, NULL,
                    FACTORY_TASK_PRIO_TMR, NULL) != pdPASS) {
        bsp_uart_debug_puts("[factory] factory_tmr create FAIL (heap)\r\n");
        return STATUS_NO_MEM;
    }

    st = cmd_uart_line_service_start();
    if (st != STATUS_OK) {
        bsp_uart_debug_puts("[factory] cmd start FAILED\r\n");
        return st;
    }
    factory_cmd_register();

    bsp_uart_debug_puts("[factory] rtos start\r\n");
    return STATUS_OK;
}
