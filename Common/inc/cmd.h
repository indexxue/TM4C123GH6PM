/**
 * @file    cmd.h
 * @brief   串口命令行框架（TM4C123 蓝牙 UART0）
 */

#ifndef CMD_H
#define CMD_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "type.h"

#define CMD_TX_MUTEX_TIMEOUT_MS (500U)
#define CMD_LINE_MAX (200U)
#define CMD_NAME_MAX (16U)
#define CMD_HELP_MAX (48U)
#define CMD_MAX_COMMANDS (32U)
#define CMD_MAX_ARGC (8U)
#define CMD_STATUS_BUF_SIZE (128U)

typedef bool (*cmd_write_fn)(const void *data, size_t len, void *user_ctx);
typedef void (*cmd_handler_t)(int argc, const char *argv[]);

typedef struct {
    char name[CMD_NAME_MAX];
    cmd_handler_t handler;
    char help[CMD_HELP_MAX];
} cmd_entry_t;

void cmd_init(cmd_write_fn write_fn, void *write_ctx);
int cmd_register(const char *name, cmd_handler_t handler, const char *help);
void cmd_reply_ok(const char *cmd, const char *value);
void cmd_reply_ng(void);
bool cmd_send(const uint8_t *data, uint16_t len);
bool cmd_send_str(const char *str);
void cmd_process_line(const char *line);
void cmd_register_defaults(void);
void cmd_uart_lock(void);
void cmd_uart_unlock(void);

/** 创建蓝牙 UART0 读行任务（幂等）。须在 UART_Init 之后调用。 */
status_t cmd_uart_line_service_start(void);

/** 校验目标槽镜像、写 NVS slot、停电机并复位；成功不返回 */
status_t cmd_boot_slot_switch(uint32_t slot);

#ifdef __cplusplus
}
#endif

#endif /* CMD_H */
