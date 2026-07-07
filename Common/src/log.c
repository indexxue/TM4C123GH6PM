/**
 * @file    log.c
 * @brief   Logging implementation (TM4C123 / FreeRTOS, default sink UART debug)
 */

#include "log.h"

#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

#include "bsp_uart.h"

#define LOG_BUFFER_SIZE 256

static SemaphoreHandle_t log_mutex;
static log_output_func_t log_output_func;
static volatile BaseType_t log_scheduler_running = pdFALSE;
static log_config_t log_config = {
    .level = LOG_LEVEL_INFO,
    .color = LOG_COLOR_NONE,
    .timestamp_enable = true,
    .file_line_enable = false,
    .initialized = false,
};

static const char *log_level_strings[] = {
    [LOG_LEVEL_NONE]    = "NONE",
    [LOG_LEVEL_FATAL]   = "FATAL",
    [LOG_LEVEL_ERROR]   = "ERROR",
    [LOG_LEVEL_WARN]    = "WARN",
    [LOG_LEVEL_INFO]    = "INFO",
    [LOG_LEVEL_DEBUG]   = "DEBUG",
    [LOG_LEVEL_VERBOSE] = "VERBOSE",
};

static void log_lock(void)
{
    if ((log_mutex != NULL) && (log_scheduler_running != pdFALSE)) {
        (void)xSemaphoreTake(log_mutex, portMAX_DELAY);
    }
}

static void log_unlock(void)
{
    if ((log_mutex != NULL) && (log_scheduler_running != pdFALSE)) {
        (void)xSemaphoreGive(log_mutex);
    }
}

static void log_default_output(const char *str, uint16_t len)
{
    uint16_t i;

    if ((str == NULL) || (len == 0U)) {
        return;
    }
    for (i = 0U; i < len; i++) {
        bsp_uart_debug_putc(str[i]);
    }
}

static void log_output_string(const char *str, uint16_t len)
{
    if ((str == NULL) || (len == 0U)) {
        return;
    }
    if (log_output_func != NULL) {
        log_output_func(str, len);
        return;
    }
    log_default_output(str, len);
}

static const char *log_get_filename(const char *filepath)
{
    const char *filename = strrchr(filepath, '/');
    if (filename != NULL) {
        return filename + 1;
    }
    filename = strrchr(filepath, '\\');
    if (filename != NULL) {
        return filename + 1;
    }
    return filepath;
}

static uint32_t log_get_timestamp_ms(void)
{
    if (log_scheduler_running == pdFALSE) {
        return 0U;
    }
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

log_level_t log_get_level(void)
{
    return log_config.level;
}

status_t log_set_level(log_level_t level)
{
    if (level >= LOG_LEVEL_MAX) {
        return STATUS_INVALID_ARG;
    }
    log_config.level = level;
    return STATUS_OK;
}

status_t log_set_color(log_color_t color)
{
    log_config.color = color;
    return STATUS_OK;
}

status_t log_set_timestamp(bool enable)
{
    log_config.timestamp_enable = enable;
    return STATUS_OK;
}

status_t log_set_file_line(bool enable)
{
    log_config.file_line_enable = enable;
    return STATUS_OK;
}

status_t log_init(log_output_func_t output_func)
{
    if (log_mutex == NULL) {
        log_mutex = xSemaphoreCreateMutex();
        if (log_mutex == NULL) {
            return STATUS_NO_MEM;
        }
    }

    log_output_func = output_func;
    log_config.initialized = true;

    return STATUS_OK;
}

void log_notify_scheduler_running(void)
{
    log_scheduler_running = pdTRUE;
}

status_t log_deinit(void)
{
    log_config.initialized = false;
    log_output_func = NULL;

    if (log_mutex != NULL) {
        vSemaphoreDelete(log_mutex);
        log_mutex = NULL;
    }

    return STATUS_OK;
}

void log_output(log_level_t level, const char *file, uint16_t line, const char *fmt, ...)
{
    char buffer[LOG_BUFFER_SIZE];
    int pos = 0;
    va_list args;

    if (!log_config.initialized || level == LOG_LEVEL_NONE || level >= LOG_LEVEL_MAX) {
        return;
    }

    if (level > log_config.level) {
        return;
    }

    log_lock();

    if (log_config.timestamp_enable) {
        uint32_t timestamp = log_get_timestamp_ms();
        int ts_len = snprintf(buffer + pos, (size_t)(LOG_BUFFER_SIZE - pos), "[%05lu] ",
                              (unsigned long)timestamp);
        if (ts_len > 0 && pos + ts_len < LOG_BUFFER_SIZE) {
            pos += ts_len;
        }
    }

    {
        const char *level_str = log_level_strings[level];
        int level_len = snprintf(buffer + pos, (size_t)(LOG_BUFFER_SIZE - pos), "[%s] ", level_str);
        if (level_len > 0 && pos + level_len < LOG_BUFFER_SIZE) {
            pos += level_len;
        }
    }

    if (log_config.file_line_enable && file != NULL) {
        const char *filename = log_get_filename(file);
        int fl_len = snprintf(buffer + pos, (size_t)(LOG_BUFFER_SIZE - pos), "%s:%u ", filename, (unsigned)line);
        if (fl_len > 0 && pos + fl_len < LOG_BUFFER_SIZE) {
            pos += fl_len;
        }
    }

    va_start(args, fmt);
    int fmt_len = vsnprintf(buffer + pos, (size_t)(LOG_BUFFER_SIZE - pos), fmt, args);
    va_end(args);

    if (fmt_len > 0 && pos + fmt_len < LOG_BUFFER_SIZE) {
        pos += fmt_len;
    }

    if (pos < LOG_BUFFER_SIZE - 2) {
        buffer[pos++] = '\r';
        buffer[pos++] = '\n';
    } else {
        pos = LOG_BUFFER_SIZE - 2;
        buffer[pos++] = '\r';
        buffer[pos++] = '\n';
    }

    log_output_string(buffer, (uint16_t)pos);
    log_unlock();
}
