/**
 * @file    rc_sub.c
 * @brief   遥测订阅 mask：ns=rc key=sub
 */

#include "rc_sub.h"

#include "log.h"
#include "nvs.h"

#include "crc32.h"

#include <string.h>

#define RC_SUB_NVS_NS      "rc"
#define RC_SUB_NVS_KEY     "sub"
#define RC_SUB_MAGIC       0x5253U /* 'R''S' */
#define RC_SUB_VERSION     1U
#define RC_SUB_OPT_MASK \
    (PROTO_CLIENT_CH_BATTERY | PROTO_CLIENT_CH_ULTRASONIC | PROTO_CLIENT_CH_MOTOR_RPM)

typedef struct {
    uint16_t magic;
    uint16_t version;
    uint32_t mask;
    uint32_t crc32;
} rc_sub_blob_t;

static uint32_t s_mask = PROTO_CLIENT_SUB_DEFAULT_MASK;

static void rc_sub_fill_crc(rc_sub_blob_t *b)
{
    rc_sub_blob_t tmp;

    if (b == NULL) {
        return;
    }
    tmp = *b;
    tmp.crc32 = 0U;
    b->crc32 = crc32_compute(&tmp, sizeof(tmp));
}

static bool_t rc_sub_validate(const rc_sub_blob_t *b)
{
    rc_sub_blob_t tmp;
    uint32_t crc;

    if (b == NULL) {
        return FALSE;
    }
    if ((b->magic != RC_SUB_MAGIC) || (b->version != RC_SUB_VERSION)) {
        return FALSE;
    }
    if ((b->mask & ~RC_SUB_OPT_MASK) != 0U) {
        return FALSE;
    }
    tmp = *b;
    tmp.crc32 = 0U;
    crc = crc32_compute(&tmp, sizeof(tmp));
    return (crc == b->crc32) ? TRUE : FALSE;
}

status_t rc_sub_init(void)
{
    rc_sub_blob_t blob;
    uint32_t len = (uint32_t)sizeof(blob);

    s_mask = PROTO_CLIENT_SUB_DEFAULT_MASK;
    if (nvs_get_blob(RC_SUB_NVS_NS, RC_SUB_NVS_KEY, &blob, &len) == STATUS_OK) {
        if ((len == (uint32_t)sizeof(blob)) && (rc_sub_validate(&blob) != FALSE)) {
            s_mask = blob.mask & RC_SUB_OPT_MASK;
            LOG_INFO("rc_sub: loaded mask=0x%08lx", (unsigned long)s_mask);
            return STATUS_OK;
        }
    }
    LOG_INFO("rc_sub: default mask=0x%08lx", (unsigned long)s_mask);
    return STATUS_OK;
}

uint32_t rc_sub_get_mask(void)
{
    return s_mask;
}

status_t rc_sub_save_mask(uint32_t optional_mask)
{
    rc_sub_blob_t blob;

    s_mask = optional_mask & RC_SUB_OPT_MASK;
    (void)memset(&blob, 0, sizeof(blob));
    blob.magic = RC_SUB_MAGIC;
    blob.version = RC_SUB_VERSION;
    blob.mask = s_mask;
    rc_sub_fill_crc(&blob);
    if (nvs_set_blob(RC_SUB_NVS_NS, RC_SUB_NVS_KEY, &blob, (uint32_t)sizeof(blob)) != STATUS_OK) {
        LOG_WARN("rc_sub: save fail");
        return STATUS_FAIL;
    }
    LOG_INFO("rc_sub: saved mask=0x%08lx", (unsigned long)s_mask);
    return STATUS_OK;
}
