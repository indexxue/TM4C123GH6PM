/**
 * @file    ota_meta.h
 * @brief   OTA 元数据状态机（NVS 持久化）
 */

#ifndef OTA_META_H
#define OTA_META_H

#include <stdint.h>
#include <stddef.h>

#include "type.h"

#define OTA_META_MAGIC              0x4F544131U   /* "OTA1" */
#define OTA_META_STRUCT_VERSION     1U
#define OTA_META_VERSION_LEN        16U
#define OTA_META_SIZE               256U
#define OTA_BOOT_ATTEMPT_MAX        3U

typedef enum {
    OTA_STATE_IDLE = 0,
    OTA_STATE_DOWNLOADING,
    OTA_STATE_READY,
    OTA_STATE_APPLYING,
    OTA_STATE_APPLY_FAILED,
    OTA_STATE_PENDING_VERIFY,
    OTA_STATE_CONFIRMED,
} ota_state_t;

typedef struct {
    uint32_t magic;
    uint32_t struct_version;
    uint32_t seq;
    uint32_t state;
    uint32_t image_size;
    uint32_t image_crc32;
    char     version[OTA_META_VERSION_LEN];
    uint32_t boot_attempts;
    uint32_t crc32;
    uint8_t  reserved[208];
} ota_meta_t;

_Static_assert(sizeof(ota_meta_t) == OTA_META_SIZE, "ota_meta_t must be 256 bytes");

status_t ota_init(void);
status_t ota_meta_read(ota_meta_t *out);
status_t ota_meta_write(const ota_meta_t *in);

void ota_meta_set_defaults(ota_meta_t *meta);
int ota_meta_is_valid(const ota_meta_t *meta);
uint32_t ota_meta_compute_crc(const ota_meta_t *meta);
const char *ota_state_to_str(ota_state_t state);

#endif /* OTA_META_H */
