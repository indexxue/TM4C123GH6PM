/**
 * @file    proto_client.h
 * @brief   蓝牙协议主机侧（遥控器 → 小车）：帧编码与 DRIVE 发送
 *
 * 帧格式与 docs/bluetooth-protocol.md / Common/src/proto.c 一致；
 * 主机发送时 FLAGS 不含 DIR_DEVICE（bit0）。
 */

#ifndef COMMON_PROTO_CLIENT_H
#define COMMON_PROTO_CLIENT_H

#include "type.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PROTO_CLIENT_DRIVE_THROTTLE_MAX   1000
#define PROTO_CLIENT_DRIVE_STEER_MAX      1000

status_t proto_client_init(void);
status_t proto_client_send_hello(void);
status_t proto_client_send_ping(void);
status_t proto_client_send_drive(int16_t throttle, int16_t steer);
status_t proto_client_send_drive_stop(void);

/**
 * 遥控更新（在 tick 同周期调用）：仅在 link UP 时发送；
 * 静止发一次 STOP；运动指令限频并跳过重复值。
 */
void proto_client_drive_update(int16_t throttle, int16_t steer, bool_t muted);

/** 周期调用：链路保活（PING）与超时停驶 */
void proto_client_tick(uint32_t period_ms);

bool_t proto_client_link_up(void);

#ifdef __cplusplus
}
#endif

#endif /* COMMON_PROTO_CLIENT_H */
