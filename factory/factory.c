/**
 * @file    factory.c
 * @brief   厂测固件：factory_evt 事件调度 + factory_tmr 周期定时 + cmd 接口
 */

#include "factory.h"

#include "board.h"
#include "bsp_sysctl.h"
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
#define FACTORY_HEARTBEAT_PERIOD_MS (1000U)

/* -------------------------------------------------------------------------- */
/* 心跳                                                                       */
/* -------------------------------------------------------------------------- */

static uint32_t s_heartbeat_count;
static uint32_t s_heartbeat_elapsed_ms;

static void factory_heartbeat_log(void)
{
    s_heartbeat_count++;
    LOG_INFO("factory:heartbeat #%lu version=%s",
             (unsigned long)s_heartbeat_count, FACTORY_VERSION);
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

static void factory_on_timer(void)
{
    s_heartbeat_elapsed_ms += FACTORY_CTRL_PERIOD_MS;
    if (s_heartbeat_elapsed_ms >= FACTORY_HEARTBEAT_PERIOD_MS) {
        s_heartbeat_elapsed_ms -= FACTORY_HEARTBEAT_PERIOD_MS;
        factory_heartbeat_log();
    }
}

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
    (void)log_set_level(LOG_LEVEL_VERBOSE);

    if (nvs_startup_finalize() != STATUS_OK) {
        LOG_WARN("factory: nvs_startup_finalize failed");
    }

    LOG_INFO("factory:ready version=%s @ 0x%08lX",
             FACTORY_VERSION, (unsigned long)FLASH_APP_B_BASE);

    for (;;) {
        event_schedule();

        if (event_is_set(EVT_ID_TIMER)) {
            button_schedule();
            factory_on_timer();
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
    Motor_Init();
    Encoder_Init();
    Line_Init();
    Board_Periph_Init();

    (void)log_init(NULL);

    LOG_INFO("factory: board init @ 0x%08lX (APP_B storage)",
             (unsigned long)FLASH_APP_B_BASE);
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
