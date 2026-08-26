/**
 * @file    rc_sub.h
 * @brief   遥控器遥测订阅 mask（NVS 持久化）
 */

#ifndef RC_SUB_H
#define RC_SUB_H

#include "proto_client.h"
#include "type.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

status_t rc_sub_init(void);

/** 当前可选通道 mask（不含常驻姿态/编码器语义，仅可选位） */
uint32_t rc_sub_get_mask(void);

/** 写 RAM + NVS；失败返回 STATUS_FAIL */
status_t rc_sub_save_mask(uint32_t optional_mask);

#ifdef __cplusplus
}
#endif

#endif /* RC_SUB_H */
