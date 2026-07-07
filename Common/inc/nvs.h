/**
 * @file    nvs.h
 * @brief   NVS 子系统：2×4 KB 页式布局 + 键值配置
 */

#ifndef NVS_H
#define NVS_H

#include <stdint.h>
#include <stddef.h>

#include "type.h"

#define NVS_PAGE_SIZE           4096U
#define NVS_PAGE_COUNT          2U
#define NVS_PAGE_HDR_SIZE       16U
#define NVS_OTA_META_OFFSET     NVS_PAGE_HDR_SIZE
#define NVS_OTA_META_SIZE       256U
#define NVS_DATA_OFFSET         (NVS_OTA_META_OFFSET + NVS_OTA_META_SIZE)

#define NVS_PAGE_MAGIC          0x4E565331U   /* "NVS1" */
#define NVS_PAGE_VERSION        1U

#define NVS_KEY_MAX             16U
#define NVS_NS_MAX              8U
#define NVS_BLOB_MAX            128U

typedef enum {
    NVS_FLASH_ALLOW_NVS = 1U << 0,
    NVS_FLASH_ALLOW_STAGING = 1U << 1,
} nvs_flash_allow_t;

status_t nvs_flash_erase(uint32_t address, uint32_t length, nvs_flash_allow_t allow);
status_t nvs_flash_program(uint32_t address, const void *data, uint32_t length,
                           nvs_flash_allow_t allow);

status_t nvs_init(void);
uint32_t nvs_active_page_index(void);
void nvs_set_active_page_index(uint32_t page_index);

status_t nvs_set_u32(const char *ns, const char *key, uint32_t value);
status_t nvs_get_u32(const char *ns, const char *key, uint32_t *value);
status_t nvs_set_blob(const char *ns, const char *key, const void *data, uint32_t len);
status_t nvs_get_blob(const char *ns, const char *key, void *data, uint32_t *len_inout);

#endif /* NVS_H */
