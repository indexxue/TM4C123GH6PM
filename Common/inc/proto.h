/**
 * @file    proto.h
 * @brief   UART0 蓝牙二进制协议层（见 docs/bluetooth-protocol.md）
 */

#ifndef PROTO_H
#define PROTO_H

#include "type.h"

#ifdef __cplusplus
extern "C" {
#endif

status_t proto_uart_service_start(void);
void proto_telemetry_tick(uint32_t period_ms);

/** 原始字节回显（绕过帧解析），用于 UART0/蓝牙链路验证 */
void proto_echo_set(bool_t enable);
bool_t proto_echo_get(void);

/** 向 UART0 发送原始数据（线程安全） */
void proto_send_raw(const uint8_t *data, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif /* PROTO_H */
