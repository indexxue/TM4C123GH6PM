/**
 * @file    rc_target.h
 * @brief   目标设备槽位（NVS rc/targets）
 */

#ifndef RC_TARGET_H
#define RC_TARGET_H

#include "type.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RC_TARGET_MAX           4U
#define RC_TARGET_NICK_LEN      12U
#define RC_TARGET_SERIAL_LEN    16U

typedef struct {
    char nickname[RC_TARGET_NICK_LEN];
    char serial[RC_TARGET_SERIAL_LEN];
    uint32_t last_caps;
} rc_target_t;

status_t rc_target_init(void);

uint8_t rc_target_count(void);
uint8_t rc_target_active_index(void);

const rc_target_t *rc_target_get(uint8_t idx);
const rc_target_t *rc_target_active(void);

/** 读当前槽 serial（空串表示任意设备） */
const char *rc_target_active_serial(void);

status_t rc_target_set_active(uint8_t idx);

/** 绑定最近一次 HELLO ACK 的 serial 到当前槽（serial 非空时） */
status_t rc_target_bind_peer_serial(uint32_t caps);

#ifdef __cplusplus
}
#endif

#endif /* RC_TARGET_H */
