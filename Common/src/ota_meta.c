/**
 * @file    ota_meta.c
 * @brief   OTA 元数据读写与上电初始化
 */

#include "ota_meta.h"

#include <string.h>

#include "crc32.h"
#include "flash_layout.h"
#include "log.h"
#include "nvs.h"
#include "nvs_flash_ops.h"

static uint32_t ota_meta_page_addr(uint32_t page_index)
{
    return FLASH_NVS_BASE + (page_index * NVS_PAGE_SIZE);
}

static const ota_meta_t *ota_meta_flash_ptr(uint32_t page_index)
{
    return (const ota_meta_t *)(ota_meta_page_addr(page_index) + NVS_OTA_META_OFFSET);
}

uint32_t ota_meta_compute_crc(const ota_meta_t *meta)
{
    ota_meta_t tmp;
    size_t crc_len = offsetof(ota_meta_t, crc32);

    memcpy(&tmp, meta, sizeof(tmp));
    tmp.crc32 = 0U;
    return crc32_compute(&tmp, crc_len);
}

void ota_meta_set_defaults(ota_meta_t *meta)
{
    memset(meta, 0, sizeof(*meta));
    meta->magic = OTA_META_MAGIC;
    meta->struct_version = OTA_META_STRUCT_VERSION;
    meta->state = OTA_STATE_IDLE;
    meta->crc32 = ota_meta_compute_crc(meta);
}

int ota_meta_is_valid(const ota_meta_t *meta)
{
    if (meta->magic != OTA_META_MAGIC) {
        return 0;
    }
    if (meta->struct_version != OTA_META_STRUCT_VERSION) {
        return 0;
    }
    if (meta->crc32 != ota_meta_compute_crc(meta)) {
        return 0;
    }
    if (meta->state > OTA_STATE_CONFIRMED) {
        return 0;
    }
    return 1;
}

const char *ota_state_to_str(ota_state_t state)
{
    switch (state) {
    case OTA_STATE_IDLE:            return "IDLE";
    case OTA_STATE_DOWNLOADING:     return "DOWNLOADING";
    case OTA_STATE_READY:           return "READY";
    case OTA_STATE_APPLYING:        return "APPLYING";
    case OTA_STATE_APPLY_FAILED:    return "APPLY_FAILED";
    case OTA_STATE_PENDING_VERIFY:  return "PENDING_VERIFY";
    case OTA_STATE_CONFIRMED:       return "CONFIRMED";
    default:                        return "UNKNOWN";
    }
}

static int ota_page_hdr_valid(uint32_t page_index)
{
    const uint32_t *raw = (const uint32_t *)ota_meta_page_addr(page_index);

    if (raw[0] != NVS_PAGE_MAGIC) {
        return 0;
    }
    if (raw[1] != NVS_PAGE_VERSION) {
        return 0;
    }
    return 1;
}

static status_t ota_meta_read_best(ota_meta_t *out)
{
    uint32_t i;
    int found = 0;
    ota_meta_t best;

    memset(&best, 0, sizeof(best));

    for (i = 0U; i < NVS_PAGE_COUNT; i++) {
        const ota_meta_t *candidate = ota_meta_flash_ptr(i);

        if (ota_page_hdr_valid(i) == 0) {
            continue;
        }
        if (ota_meta_is_valid(candidate) == 0) {
            continue;
        }
        if ((found == 0) || (candidate->seq >= best.seq)) {
            best = *candidate;
            found = 1;
        }
    }

    if (found == 0) {
        ota_meta_set_defaults(out);
        return STATUS_FAIL;
    }

    *out = best;
    return STATUS_OK;
}

status_t ota_meta_read(ota_meta_t *out)
{
    if (out == NULL) {
        return STATUS_INVALID_ARG;
    }

    if (ota_meta_read_best(out) == STATUS_OK) {
        return STATUS_OK;
    }

    ota_meta_set_defaults(out);
    return STATUS_OK;
}

static status_t ota_meta_program_page(uint32_t page_index, uint32_t seq, const ota_meta_t *meta,
                                      const uint8_t *data_tail, uint32_t data_tail_len)
{
    typedef struct {
        uint32_t magic;
        uint32_t version;
        uint32_t seq;
        uint32_t hdr_crc32;
    } page_hdr_t;

    page_hdr_t hdr;
    uint32_t page_base = ota_meta_page_addr(page_index);
    status_t st;

    st = nvs_flash_erase(page_base, NVS_PAGE_SIZE, NVS_FLASH_ALLOW_NVS);
    if (st != STATUS_OK) {
        return st;
    }

    hdr.magic = NVS_PAGE_MAGIC;
    hdr.version = NVS_PAGE_VERSION;
    hdr.seq = seq;
    {
        uint32_t crc = CRC32_INIT_VALUE;
        crc = crc32_update(crc, &hdr.magic, sizeof(hdr.magic));
        crc = crc32_update(crc, &hdr.version, sizeof(hdr.version));
        crc = crc32_update(crc, &hdr.seq, sizeof(hdr.seq));
        hdr.hdr_crc32 = crc32_finalize(crc);
    }

    st = nvs_flash_program(page_base, &hdr, sizeof(hdr), NVS_FLASH_ALLOW_NVS);
    if (st != STATUS_OK) {
        return st;
    }

    st = nvs_flash_program(page_base + NVS_OTA_META_OFFSET, meta, OTA_META_SIZE,
                           NVS_FLASH_ALLOW_NVS);
    if (st != STATUS_OK) {
        return st;
    }

    if ((data_tail != NULL) && (data_tail_len > 0U)) {
        st = nvs_flash_program(page_base + NVS_DATA_OFFSET, data_tail, data_tail_len,
                               NVS_FLASH_ALLOW_NVS);
        if (st != STATUS_OK) {
            return st;
        }
    }

    return STATUS_OK;
}

status_t ota_meta_write(const ota_meta_t *in)
{
    ota_meta_t meta;
    uint32_t active = nvs_active_page_index();
    uint32_t inactive = (active + 1U) % NVS_PAGE_COUNT;
    uint32_t next_seq = 1U;
    const uint8_t *active_page = (const uint8_t *)ota_meta_page_addr(active);
    uint32_t tail_len;
    status_t st;

    if (in == NULL) {
        return STATUS_INVALID_ARG;
    }

    memcpy(&meta, in, sizeof(meta));
    meta.magic = OTA_META_MAGIC;
    meta.struct_version = OTA_META_STRUCT_VERSION;

    {
        ota_meta_t current;

        if (ota_meta_read_best(&current) == STATUS_OK) {
            next_seq = current.seq + 1U;
        } else {
            next_seq = 1U;
        }
        meta.seq = next_seq;
    }

    meta.crc32 = ota_meta_compute_crc(&meta);

    tail_len = NVS_PAGE_SIZE - NVS_DATA_OFFSET;
    st = ota_meta_program_page(inactive, next_seq, &meta, active_page + NVS_DATA_OFFSET, tail_len);
    if (st != STATUS_OK) {
        return st;
    }

    nvs_set_active_page_index(inactive);
    return STATUS_OK;
}

status_t ota_init(void)
{
    ota_meta_t meta;
    status_t st;

    st = nvs_init();
    if (st != STATUS_OK) {
        LOG_ERROR("nvs_init failed");
        return st;
    }

    if (ota_meta_read_best(&meta) != STATUS_OK) {
        LOG_INFO("ota_meta: first init, writing defaults");
        ota_meta_set_defaults(&meta);
        st = ota_meta_write(&meta);
        if (st != STATUS_OK) {
            LOG_ERROR("ota_meta_write defaults failed");
            return st;
        }
    }

    if (meta.state == OTA_STATE_DOWNLOADING) {
        LOG_WARN("ota_meta: clear stale DOWNLOADING");
        meta.state = OTA_STATE_IDLE;
        st = ota_meta_write(&meta);
        if (st != STATUS_OK) {
            LOG_ERROR("ota_meta clear DOWNLOADING failed");
            return st;
        }
    }

    LOG_INFO("ota_meta: state=%s seq=%lu", ota_state_to_str((ota_state_t)meta.state),
             (unsigned long)meta.seq);
    return STATUS_OK;
}
