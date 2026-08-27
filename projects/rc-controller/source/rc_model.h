/**
 * @file    rc_model.h
 * @brief   多模型配置：NVS 持久化（最多 RC_MODEL_MAX 套）
 */

#ifndef RC_MODEL_H
#define RC_MODEL_H

#include "proto_client.h"
#include "type.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RC_MODEL_MAX           8U
#define RC_MODEL_NAME_LEN      8U
#define RC_MODEL_DEFAULT_COUNT 3U

typedef enum {
    RC_MODEL_INPUT_STICK = 0,
    RC_MODEL_INPUT_IMU_TILT = 1,
} rc_model_input_src_t;

typedef struct {
    char name[RC_MODEL_NAME_LEN];
    rc_model_input_src_t input_src;
    uint8_t target_slot;
    uint32_t sub_mask;
} rc_model_t;

status_t rc_model_init(void);

uint8_t rc_model_count(void);
uint8_t rc_model_active_index(void);

/** 只读；idx >= count 时返回 NULL */
const rc_model_t *rc_model_get(uint8_t idx);
const rc_model_t *rc_model_active(void);

status_t rc_model_set_active(uint8_t idx);

/** 下一套模型；写 NVS；同步 rc_sub mask */
status_t rc_model_cycle_next(void);

/** 应用当前模型 sub_mask 到 rc_sub（内存 + NVS） */
status_t rc_model_apply_sub(void);

#ifdef __cplusplus
}
#endif

#endif /* RC_MODEL_H */
