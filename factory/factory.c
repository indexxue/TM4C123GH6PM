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
#include "log.h"
#include "nvs.h"

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

static void factory_cmd_register(void)
{
    (void)cmd_register("ftm", factory_cmd_ftm, "ftm [version] factory status");
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

    for (;;) {
        event_schedule();

        if (event_is_set(EVT_ID_TIMER)) {
            button_schedule();
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
    Motor_Init();
    Encoder_Init();
    Line_Init();
    Board_Periph_Init();

    (void)log_init(NULL);
}

status_t Factory_Start(void)
{
    status_t st;

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
        return st;
    }
    factory_cmd_register();

    return STATUS_OK;
}
