/**
 * @file    cmd.c
 * @brief   调试串口命令行框架（TM4C123 UART7）
 */

#include "cmd.h"

#include "boot_slot.h"
#include "log.h"
#include "nvs.h"
#include "flash_layout.h"
#include "board.h"
#include "button.h"
#include "chassis.h"
#include "bsp_adc.h"
#include "bsp_i2c.h"
#include "bsp_sysctl.h"

#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

#include "inc/hw_memmap.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef BOARD_MOTOR_COUNT
#define BOARD_MOTOR_COUNT 4U
#endif

static cmd_write_fn s_write_fn;
static void *s_write_ctx;
static cmd_entry_t s_cmd_table[CMD_MAX_COMMANDS];
static int s_cmd_count;
static SemaphoreHandle_t s_uart_mutex;
static TaskHandle_t s_reader_task;

static bool cmd_write_debug(const void *data, size_t len, void *user_ctx)
{
    const uint8_t *p;
    size_t i;

    (void)user_ctx;
    if ((data == NULL) || (len == 0U)) {
        return true;
    }
    p = (const uint8_t *)data;
    for (i = 0U; i < len; i++) {
        UART_Debug_Putc((char)p[i]);
    }
    return true;
}

void cmd_init(cmd_write_fn write_fn, void *write_ctx)
{
    s_write_fn = (write_fn != NULL) ? write_fn : cmd_write_debug;
    s_write_ctx = write_ctx;
    s_cmd_count = 0;
    (void)memset(s_cmd_table, 0, sizeof(s_cmd_table));
    if (s_uart_mutex == NULL) {
        s_uart_mutex = xSemaphoreCreateRecursiveMutex();
    }
}

int cmd_register(const char *name, cmd_handler_t handler, const char *help)
{
    size_t nlen;

    if ((name == NULL) || (handler == NULL) || (s_cmd_count >= (int)CMD_MAX_COMMANDS)) {
        return -1;
    }
    nlen = strlen(name);
    if ((nlen == 0U) || (nlen >= CMD_NAME_MAX)) {
        return -1;
    }
    (void)strncpy(s_cmd_table[s_cmd_count].name, name, CMD_NAME_MAX - 1U);
    s_cmd_table[s_cmd_count].name[CMD_NAME_MAX - 1U] = '\0';
    s_cmd_table[s_cmd_count].handler = handler;
    if (help != NULL) {
        (void)strncpy(s_cmd_table[s_cmd_count].help, help, CMD_HELP_MAX - 1U);
        s_cmd_table[s_cmd_count].help[CMD_HELP_MAX - 1U] = '\0';
    } else {
        s_cmd_table[s_cmd_count].help[0] = '\0';
    }
    s_cmd_count++;
    return 0;
}

static bool cmd_send_unlocked(const uint8_t *data, uint16_t len)
{
    if ((data == NULL) || (len == 0U) || (s_write_fn == NULL)) {
        return false;
    }
    return s_write_fn(data, (size_t)len, s_write_ctx);
}

bool cmd_send(const uint8_t *data, uint16_t len)
{
    bool ok;

    if ((data == NULL) || (len == 0U)) {
        return false;
    }
    if (s_uart_mutex != NULL) {
        if (xSemaphoreTakeRecursive(s_uart_mutex, pdMS_TO_TICKS(CMD_TX_MUTEX_TIMEOUT_MS)) != pdTRUE) {
            return false;
        }
    }
    ok = cmd_send_unlocked(data, len);
    if (s_uart_mutex != NULL) {
        (void)xSemaphoreGiveRecursive(s_uart_mutex);
    }
    return ok;
}

void cmd_uart_lock(void)
{
    if (s_uart_mutex != NULL) {
        (void)xSemaphoreTakeRecursive(s_uart_mutex, portMAX_DELAY);
    }
}

void cmd_uart_unlock(void)
{
    if (s_uart_mutex != NULL) {
        (void)xSemaphoreGiveRecursive(s_uart_mutex);
    }
}

bool cmd_send_str(const char *str)
{
    size_t len;

    if (str == NULL) {
        return false;
    }
    len = strlen(str);
    if (len == 0U) {
        return true;
    }
    if (len > 0xFFFFU) {
        return false;
    }
    return cmd_send((const uint8_t *)str, (uint16_t)len);
}

void cmd_reply_ok(const char *cmd, const char *value)
{
    char buf[CMD_LINE_MAX + 16U];
    int n;

    n = snprintf(buf, sizeof(buf), "%s:%s\r\n", (cmd != NULL) ? cmd : "", (value != NULL) ? value : "");
    if ((n > 0) && ((size_t)n < sizeof(buf))) {
        (void)cmd_send((const uint8_t *)buf, (uint16_t)n);
    }
}

void cmd_reply_ng(void)
{
    (void)cmd_send_str("ng\r\n");
}

static void trim_and_tokenize(char *line, const char *argv[], int *argc, int max_argc)
{
    *argc = 0;
    while ((*line == ' ') || (*line == '\t')) {
        line++;
    }
    while ((*line != '\0') && (*argc < max_argc)) {
        argv[*argc] = line;
        (*argc)++;
        while ((*line != '\0') && (*line != ' ') && (*line != '\t') && (*line != '\r') && (*line != '\n')) {
            *line = (char)tolower((unsigned char)*line);
            line++;
        }
        if (*line == '\0') {
            break;
        }
        *line = '\0';
        line++;
        while ((*line == ' ') || (*line == '\t')) {
            line++;
        }
    }
}

static void do_help(void)
{
    char buf[CMD_STATUS_BUF_SIZE];
    int n;

    n = snprintf(buf, sizeof(buf), "help: list commands\r\n");
    if ((n > 0) && ((size_t)n < sizeof(buf))) {
        (void)cmd_send((const uint8_t *)buf, (uint16_t)n);
    }
    for (int i = 0; i < s_cmd_count; i++) {
        const char *h = s_cmd_table[i].help;
        if (h[0] == '\0') {
            h = "";
        }
        n = snprintf(buf, sizeof(buf), "%s: %s\r\n", s_cmd_table[i].name, h);
        if ((n > 0) && ((size_t)n < sizeof(buf))) {
            (void)cmd_send((const uint8_t *)buf, (uint16_t)n);
        }
    }
}

static void cmd_process_line_locked(const char *line)
{
    char buf[CMD_LINE_MAX];
    size_t len;
    const char *argv[CMD_MAX_ARGC];
    int argc = 0;

    if (line == NULL) {
        cmd_reply_ng();
        return;
    }
    len = strlen(line);
    if (len >= sizeof(buf)) {
        len = sizeof(buf) - 1U;
    }
    (void)memcpy(buf, line, len);
    buf[len] = '\0';
    for (size_t i = 0U; i < len; i++) {
        if ((buf[i] == '\r') || (buf[i] == '\n')) {
            buf[i] = '\0';
            break;
        }
    }
    trim_and_tokenize(buf, argv, &argc, (int)CMD_MAX_ARGC);
    if (argc == 0) {
        cmd_reply_ng();
        return;
    }
    if (strcmp(argv[0], "help") == 0) {
        do_help();
        return;
    }
    for (int i = 0; i < s_cmd_count; i++) {
        if (strcmp(argv[0], s_cmd_table[i].name) == 0) {
            s_cmd_table[i].handler(argc, argv);
            return;
        }
    }
    cmd_reply_ng();
}

void cmd_process_line(const char *line)
{
    if (s_uart_mutex != NULL) {
        (void)xSemaphoreTakeRecursive(s_uart_mutex, portMAX_DELAY);
    }
    cmd_process_line_locked(line);
    if (s_uart_mutex != NULL) {
        (void)xSemaphoreGiveRecursive(s_uart_mutex);
    }
}

static void cmd_reboot(int argc, const char *argv[])
{
    (void)argc;
    (void)argv;
    cmd_reply_ok("reboot", "now");
    bsp_system_reset();
}

static void cmd_log(int argc, const char *argv[])
{
    (void)argc;
    (void)argv;
    LOG_INFO("[CMD] uart cmd log test ok");
    cmd_reply_ok("log", "ok");
}

static void cmd_version(int argc, const char *argv[])
{
    (void)argc;
    (void)argv;
    cmd_reply_ok("version", "tm4c123-car");
}

static void cmd_i2c(int argc, const char *argv[])
{
    char buf[CMD_STATUS_BUF_SIZE];
    size_t len = 0U;
    int found = 0;

    (void)argc;
    (void)argv;

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

    if (found == 0) {
        (void)strncpy(buf, "none", sizeof(buf) - 1U);
        buf[sizeof(buf) - 1U] = '\0';
    }

    bsp_i2c0_gpio_scan_end_idle_high();
    cmd_reply_ok("i2c", buf);
}

#if defined(NVS_CMD_RAW_KV)
static void cmd_nvs(int argc, const char *argv[])
{
    char buf[CMD_STATUS_BUF_SIZE];

    if ((argc == 4) && (strcmp(argv[1], "get") == 0)) {
        uint32_t val = 0U;
        if (nvs_get_u32(argv[2], argv[3], &val) == STATUS_OK) {
            (void)snprintf(buf, sizeof(buf), "%lu", (unsigned long)val);
            cmd_reply_ok("nvs", buf);
            return;
        }
        cmd_reply_ng();
        return;
    }

    cmd_reply_ng();
}
#endif

static void cmd_param(int argc, const char *argv[])
{
    nvs_spd_limit_t limit;
    nvs_pid3_t pid;
    const nvs_cfg_t *cfg;

    if ((argc == 3) && (strcmp(argv[1], "mot_dir") == 0)) {
        u32_t mask = (u32_t)strtoul(argv[2], NULL, 0);
        if (nvs_param_set_motor_dir_mask(mask, NVS_WRITE_SRC_CMD) == STATUS_OK) {
            cmd_reply_ok("param", "ok");
            return;
        }
        cmd_reply_ng();
        return;
    }

    if ((argc == 3) && (strcmp(argv[1], "max_rpm") == 0)) {
        cfg = nvs_cfg_get();
        limit = cfg->spd_limit;
        limit.max_rpm = (f32_t)strtof(argv[2], NULL);
        if (nvs_param_set_spd_limit(&limit, NVS_WRITE_SRC_CMD) == STATUS_OK) {
            cmd_reply_ok("param", "ok");
            return;
        }
        cmd_reply_ng();
        return;
    }

    if ((argc == 5) && (strcmp(argv[1], "pid_spd") == 0)) {
        pid.kp = (f32_t)strtof(argv[2], NULL);
        pid.ki = (f32_t)strtof(argv[3], NULL);
        pid.kd = (f32_t)strtof(argv[4], NULL);
        if (nvs_param_set_pid_speed(&pid, NVS_WRITE_SRC_CMD) == STATUS_OK) {
            cmd_reply_ok("param", "ok");
            return;
        }
        cmd_reply_ng();
        return;
    }

    if ((argc == 5) && (strcmp(argv[1], "pid_line") == 0)) {
        pid.kp = (f32_t)strtof(argv[2], NULL);
        pid.ki = (f32_t)strtof(argv[3], NULL);
        pid.kd = (f32_t)strtof(argv[4], NULL);
        if (nvs_param_set_pid_line(&pid, NVS_WRITE_SRC_CMD) == STATUS_OK) {
            chassis_reload_line_pid_gains();
            cmd_reply_ok("param", "ok");
            return;
        }
        cmd_reply_ng();
        return;
    }

    if ((argc == 3) && (strcmp(argv[1], "line_th") == 0)) {
        nvs_line_threshold_t th;
        u16_t v;
        u8_t i;

        v = (u16_t)strtoul(argv[2], NULL, 0);
        if (v > 4095U) {
            cmd_reply_ng();
            return;
        }
        for (i = 0U; i < NVS_CFG_LINE_SENSOR_COUNT; i++) {
            th.threshold[i] = v;
        }
        if (nvs_param_set_line_threshold(&th, NVS_WRITE_SRC_CMD) == STATUS_OK) {
            cmd_reply_ok("param", "ok");
            return;
        }
        cmd_reply_ng();
        return;
    }

    if ((argc == 4) && (strcmp(argv[1], "line_th") == 0)) {
        nvs_line_threshold_t th;
        u32_t idx;
        u16_t v;

        idx = strtoul(argv[2], NULL, 0);
        v = (u16_t)strtoul(argv[3], NULL, 0);
        if ((idx >= NVS_CFG_LINE_SENSOR_COUNT) || (v > 4095U)) {
            cmd_reply_ng();
            return;
        }
        th = nvs_cfg_get()->line_threshold;
        th.threshold[idx] = v;
        if (nvs_param_set_line_threshold(&th, NVS_WRITE_SRC_CMD) == STATUS_OK) {
            cmd_reply_ok("param", "ok");
            return;
        }
        cmd_reply_ng();
        return;
    }

    if ((argc == 3) && (strcmp(argv[1], "line_pol") == 0)) {
        u32_t pol = strtoul(argv[2], NULL, 0);

        if (pol > 1U) {
            cmd_reply_ng();
            return;
        }
        if (nvs_param_set_line_polarity((u8_t)pol, NVS_WRITE_SRC_CMD) == STATUS_OK) {
            cmd_reply_ok("param", "ok");
            return;
        }
        cmd_reply_ng();
        return;
    }

    if ((argc == 3) && (strcmp(argv[1], "line_base") == 0)) {
        f32_t rpm = (f32_t)strtof(argv[2], NULL);

        if (nvs_param_set_line_base_rpm(rpm, NVS_WRITE_SRC_CMD) == STATUS_OK) {
            cmd_reply_ok("param", "ok");
            return;
        }
        cmd_reply_ng();
        return;
    }

    if ((argc == 5) && (strcmp(argv[1], "pid_yaw") == 0)) {
        pid.kp = (f32_t)strtof(argv[2], NULL);
        pid.ki = (f32_t)strtof(argv[3], NULL);
        pid.kd = (f32_t)strtof(argv[4], NULL);
        if (nvs_param_set_pid_yaw(&pid, NVS_WRITE_SRC_CMD) == STATUS_OK) {
            chassis_reload_angle_pid_gains();
            cmd_reply_ok("param", "ok");
            return;
        }
        cmd_reply_ng();
        return;
    }

    if ((argc == 5) && (strcmp(argv[1], "pid_dist") == 0)) {
        pid.kp = (f32_t)strtof(argv[2], NULL);
        pid.ki = (f32_t)strtof(argv[3], NULL);
        pid.kd = (f32_t)strtof(argv[4], NULL);
        if (nvs_param_set_pid_dist(&pid, NVS_WRITE_SRC_CMD) == STATUS_OK) {
            chassis_reload_distance_pid_gains();
            cmd_reply_ok("param", "ok");
            return;
        }
        cmd_reply_ng();
        return;
    }

    cmd_reply_ng();
}

static void cmd_line(int argc, const char *argv[])
{
    s32_t base_rpm = 0;

    if ((argc == 2) && (strcmp(argv[1], "start") == 0)) {
        chassis_set_line_follow(0);
        cmd_reply_ok("line", "start");
        return;
    }
    if ((argc == 3) && (strcmp(argv[1], "start") == 0)) {
        base_rpm = (s32_t)strtol(argv[2], NULL, 0);
        if ((base_rpm < 0) || (base_rpm > 1200)) {
            cmd_reply_ng();
            return;
        }
        chassis_set_line_follow(base_rpm);
        cmd_reply_ok("line", "start");
        return;
    }
    if ((argc == 2) && (strcmp(argv[1], "stop") == 0)) {
        chassis_stop();
        cmd_reply_ok("line", "stop");
        return;
    }
    cmd_reply_ng();
}

static void cmd_cfg(int argc, const char *argv[])
{
    char buf[CMD_STATUS_BUF_SIZE];
    const nvs_cfg_t *cfg;

    if ((argc == 2) && (strcmp(argv[1], "reset") == 0)) {
        if (nvs_factory_reset() == STATUS_OK) {
            cmd_reply_ok("cfg", "user reset ok");
            return;
        }
        cmd_reply_ng();
        return;
    }

    if ((argc == 2) && (strcmp(argv[1], "show") == 0)) {
        cfg = nvs_cfg_get();
        (void)snprintf(buf, sizeof(buf),
                       "schema=%lu hw=%lu mode=%lu mot_dir=0x%lX "
                       "max_rpm=%.0f pid_spd=%.2f/%.2f/%.2f serial=%s",
                       (unsigned long)cfg->schema_version,
                       (unsigned long)cfg->hw_rev,
                       (unsigned long)cfg->last_mode,
                       (unsigned long)cfg->motor_dir_mask,
                       (double)cfg->spd_limit.max_rpm,
                       (double)cfg->pid_speed.kp, (double)cfg->pid_speed.ki,
                       (double)cfg->pid_speed.kd,
                       (cfg->serial[0] != '\0') ? cfg->serial : "-");
        cmd_reply_ok("cfg", buf);
        return;
    }

    cmd_reply_ng();
}

static void cmd_motor(int argc, const char *argv[])
{
    uint8_t motor_id;
    int32_t rpm;

    if (argc != 3) {
        cmd_reply_ng();
        return;
    }

    motor_id = (uint8_t)strtoul(argv[1], NULL, 0);
    rpm = (int32_t)strtol(argv[2], NULL, 0);
    if ((motor_id < 1U) || (motor_id > BOARD_MOTOR_COUNT)) {
        cmd_reply_ng();
        return;
    }
    Motor_SetSpeed(motor_id, rpm);
    cmd_reply_ok("motor", "ok");
}

static void cmd_adc(int argc, const char *argv[])
{
    uint32_t bat_raw = 0U;
    uint32_t btn_raw = 0U;
    char buf[CMD_STATUS_BUF_SIZE];

    if ((argc >= 2) && (strcmp(argv[1], "line") == 0)) {
        uint16_t line[LINE_SENSOR_COUNT];
        static const char *const pins[LINE_SENSOR_COUNT] = {
            "PD3", "PD2", "PD1", "PD0", "PE5", "PE4"
        };
        size_t i;
        int n;

        if (!Line_IsReady() || !Line_Sample(line, LINE_SENSOR_COUNT)) {
            cmd_reply_ng();
            return;
        }
        n = snprintf(buf, sizeof(buf), "line");
        for (i = 0U; (i < LINE_SENSOR_COUNT) && (n > 0) && ((size_t)n < sizeof(buf)); i++) {
            int w = snprintf(buf + n, sizeof(buf) - (size_t)n,
                             " L%u(%s)=%u",
                             (unsigned)(i + 1U), pins[i], (unsigned)line[i]);
            if (w < 0) {
                break;
            }
            n += w;
        }
        cmd_reply_ok("adc", buf);
        return;
    }

    (void)argc;
    (void)argv;

    if (!bsp_adc_sample_one(&BOARD_BATTERY_ADC_CFG, &bat_raw)) {
        cmd_reply_ng();
        return;
    }

    if (!button_adc_raw_get(&btn_raw)) {
        cmd_reply_ng();
        return;
    }

    (void)snprintf(buf, sizeof(buf), "bat_raw=%lu btn_raw=%lu btn=%s",
                   (unsigned long)bat_raw, (unsigned long)btn_raw,
                   button_id_to_str(button_adc_pressed_id()));
    cmd_reply_ok("adc", buf);
}

status_t cmd_boot_slot_switch(uint32_t slot)
{
    return boot_slot_switch(slot);
}

static void cmd_ftmenter(int argc, const char *argv[])
{
    (void)argc;
    (void)argv;

    /* 量产不开串口进厂测（用 OK 长按）；厂测槽内本命令无意义 */
#if defined(FLASH_FACTORY_SLOT) || defined(FLASH_APP_A_SLOT)
    cmd_reply_ng();
    return;
#else
    if (!boot_image_is_valid(FLASH_APP_B_BASE)) {
        cmd_reply_ng();
        return;
    }
    cmd_reply_ok("ftmenter", "reset");
    (void)cmd_boot_slot_switch(BOOT_SLOT_B);
#endif
}

static void cmd_ftmexit(int argc, const char *argv[])
{
    (void)argc;
    (void)argv;

#if defined(FLASH_APP_A_SLOT)
    cmd_reply_ng();
    return;
#else
    if (!boot_image_is_valid(FLASH_APP_A_BASE)) {
        cmd_reply_ng();
        return;
    }
    cmd_reply_ok("ftmexit", "reset");
    (void)cmd_boot_slot_switch(BOOT_SLOT_A);
#endif
}

static void cmd_slot(int argc, const char *argv[])
{
    char buf[CMD_STATUS_BUF_SIZE];
    uint32_t slot = BOOT_SLOT_A;

    (void)argc;
    (void)argv;

    if (nvs_boot_slot_get(&slot) != STATUS_OK) {
        cmd_reply_ng();
        return;
    }

    (void)snprintf(buf, sizeof(buf), "%lu", (unsigned long)slot);
    cmd_reply_ok("slot", buf);
}

void cmd_register_defaults(void)
{
    (void)cmd_register("reboot", cmd_reboot, "software reset");
    (void)cmd_register("log", cmd_log, "emit one log line + ok");
    (void)cmd_register("version", cmd_version, "firmware version string");
    (void)cmd_register("i2c", cmd_i2c, "scan I2C0 (addr list)");
    (void)cmd_register("motor", cmd_motor, "motor <id 1..N> <rpm>");
    (void)cmd_register("adc", cmd_adc, "adc [line] — bat/btn 或 6 路循迹 ADC");
    (void)cmd_register("ftmenter", cmd_ftmenter, "ng on app/factory (use OK long-press)");
    (void)cmd_register("ftmexit", cmd_ftmexit, "switch to app slot (APP_A, factory only)");
    (void)cmd_register("slot", cmd_slot, "show boot slot (0=A 1=B)");
#if defined(NVS_CMD_RAW_KV)
    (void)cmd_register("nvs", cmd_nvs, "nvs get <ns> <key>");
#endif
    (void)cmd_register("param", cmd_param,
                       "param mot_dir|max_rpm|pid_spd|pid_line|pid_yaw|pid_dist|line_th|line_pol|line_base ...");
    (void)cmd_register("line", cmd_line, "line start [base_rpm]|stop");
    (void)cmd_register("cfg", cmd_cfg, "cfg show|reset");
}

#define CMD_READER_STACK_WORDS (1024U)
#define CMD_READER_PRIORITY (2U)
#define CMD_READER_POLL_MS (20U)

static void cmd_reader_task(void *arg)
{
    char line[CMD_LINE_MAX];
    size_t li = 0U;

    (void)arg;
    for (;;) {
        char ch;

        if (UART_Debug_Getc(&ch) == 0) {
            vTaskDelay(pdMS_TO_TICKS(CMD_READER_POLL_MS));
            continue;
        }
        if ((ch == '\b') || (ch == 0x7FU)) {
            if (li > 0U) {
                li--;
            }
            continue;
        }
        if ((ch == '\r') || (ch == '\n')) {
            if (li > 0U) {
                line[li] = '\0';
                cmd_process_line(line);
                li = 0U;
            }
            continue;
        }
        if (li < (sizeof(line) - 1U)) {
            line[li++] = ch;
            (void)UART_Debug_Putc(ch);
        }
    }
}

status_t cmd_uart_line_service_start(void)
{
    if (s_reader_task != NULL) {
        return STATUS_OK;
    }
    cmd_init(cmd_write_debug, NULL);
    cmd_register_defaults();
    if (xTaskCreate(cmd_reader_task, CMD_TASK_NAME_READER, CMD_READER_STACK_WORDS, NULL, CMD_READER_PRIORITY,
                    &s_reader_task) != pdPASS) {
        LOG_ERROR("cmd: create reader task failed");
        s_reader_task = NULL;
        return STATUS_FAIL;
    }
    return STATUS_OK;
}
