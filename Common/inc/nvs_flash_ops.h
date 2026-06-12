/**
 * @file    nvs_flash_ops.h
 * @brief   片上 Flash 擦写封装（带分区地址守卫）
 */

#ifndef NVS_FLASH_OPS_H
#define NVS_FLASH_OPS_H

#include <stdint.h>

#include "type.h"

typedef enum {
    NVS_FLASH_ALLOW_NVS = 1U << 0,
    NVS_FLASH_ALLOW_STAGING = 1U << 1,
} nvs_flash_allow_t;

status_t nvs_flash_erase(uint32_t address, uint32_t length, nvs_flash_allow_t allow);
status_t nvs_flash_program(uint32_t address, const void *data, uint32_t length,
                           nvs_flash_allow_t allow);

#endif /* NVS_FLASH_OPS_H */
