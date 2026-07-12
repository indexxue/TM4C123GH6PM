/**
 * @file    proto.h
 * @brief   UART0 蓝牙二进制协议层
 *
 * 速度环相关命令（小端）：
 * - PROTO_CMD_SET_SPEED (0x0032)：payload[0]=format；
 *   0: [motor_id u8][rpm i32]；
 *   1: [left_rpm i32][right_rpm i32]；
 *   2: [M1..M4 rpm i32 ×4]
 * - PROTO_CMD_SPEED_STOP (0x0033)：停止速度环并清零输出
 * 订阅 PROTO_CH_MOTOR_RPM (bit5) 可推送四轮 RPM（push ch=5）
 */

#ifndef PROTO_H
#define PROTO_H

#include "type.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PROTO_CMD_SET_SPEED         0x0032U
#define PROTO_CMD_SPEED_STOP        0x0033U

#define PROTO_CAP_SPEED_LOOP        (1U << 4)
#define PROTO_CH_MOTOR_RPM          (1U << 5)

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
