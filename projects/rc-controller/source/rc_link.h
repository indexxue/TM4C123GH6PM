/**
 * @file    rc_link.h
 * @brief   遥控器链路层：手动建链 + proto_client 封装
 */

#ifndef RC_LINK_H
#define RC_LINK_H

#include "proto_client.h"
#include "type.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    RC_LINK_OFF = 0,
    RC_LINK_CONNECTING,
    RC_LINK_CONNECTED,
} rc_link_state_t;

status_t rc_link_init(void);
void rc_link_tick(uint32_t dt_ms);

rc_link_state_t rc_link_state(void);
bool_t rc_link_up(void);

/** JS1：开始 HELLO burst；已在 CONNECTED 时无操作 */
status_t rc_link_connect(void);

/** CONNECTING 时 JS1 取消 */
void rc_link_cancel_connect(void);

/** 最近一次建链 HELLO serial 与目标槽不匹配 */
bool_t rc_link_wrong_device(void);

status_t rc_link_send_drive_stop(void);
status_t rc_link_subscribe(uint32_t optional_mask);
status_t rc_link_unsubscribe_optional(void);
void rc_link_drive_update(int16_t throttle, int16_t steer, bool_t muted);

void rc_link_telem_clear(void);
void rc_link_telem_get(proto_client_telem_t *out);

#ifdef __cplusplus
}
#endif

#endif /* RC_LINK_H */
