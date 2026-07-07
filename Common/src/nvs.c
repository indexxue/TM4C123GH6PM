/**
 * @file    nvs.c
 * @brief   NVS 页式存储（2×4 KB 轮换）、配置键与 Flash 擦写封装
 */

#include "nvs.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "crc32.h"
#include "flash_layout.h"

#include "driverlib/flash.h"
#include "driverlib/interrupt.h"

static status_t nvs_flash_check_range(uint32_t address, uint32_t length, nvs_flash_allow_t allow)
{
    uint32_t end;

    if (length == 0U) {
        return STATUS_INVALID_ARG;
    }

    if ((address % FLASH_PROGRAM_ALIGN) != 0U) {
        return STATUS_INVALID_ARG;
    }

    if ((length % FLASH_PROGRAM_ALIGN) != 0U) {
        return STATUS_INVALID_ARG;
    }

    end = address + length - 1U;
    if (end < address) {
        return STATUS_INVALID_ARG;
    }

    if (flash_addr_in_range(address, FLASH_BL_BASE, FLASH_BL_SIZE) ||
        flash_addr_in_range(end, FLASH_BL_BASE, FLASH_BL_SIZE)) {
        return STATUS_INVALID_ARG;
    }

    if (flash_addr_in_range(address, FLASH_APP_A_BASE, FLASH_APP_A_SIZE) ||
        flash_addr_in_range(end, FLASH_APP_A_BASE, FLASH_APP_A_SIZE)) {
        return STATUS_INVALID_ARG;
    }

    if ((allow & NVS_FLASH_ALLOW_NVS) != 0U) {
        if (flash_addr_in_range(address, FLASH_NVS_BASE, FLASH_NVS_SIZE) &&
            flash_addr_in_range(end, FLASH_NVS_BASE, FLASH_NVS_SIZE)) {
            return STATUS_OK;
        }
    }

    if ((allow & NVS_FLASH_ALLOW_STAGING) != 0U) {
        if (flash_addr_in_range(address, FLASH_STAGING_BASE, FLASH_STAGING_SIZE) &&
            flash_addr_in_range(end, FLASH_STAGING_BASE, FLASH_STAGING_SIZE)) {
            return STATUS_OK;
        }
    }

    return STATUS_INVALID_ARG;
}

status_t nvs_flash_erase(uint32_t address, uint32_t length, nvs_flash_allow_t allow)
{
    uint32_t offset;
    int32_t rc;

    if (nvs_flash_check_range(address, length, allow) != STATUS_OK) {
        return STATUS_INVALID_ARG;
    }

    for (offset = 0U; offset < length; offset += FLASH_ERASE_BLOCK_SIZE) {
        uint32_t block_addr = address + offset;

        IntMasterDisable();
        rc = FlashErase(block_addr);
        IntMasterEnable();

        if (rc != 0) {
            return STATUS_FAIL;
        }
    }

    return STATUS_OK;
}

status_t nvs_flash_program(uint32_t address, const void *data, uint32_t length,
                           nvs_flash_allow_t allow)
{
    int32_t rc;
    const uint32_t *words;

    if ((data == NULL) || (length == 0U)) {
        return STATUS_INVALID_ARG;
    }

    if (nvs_flash_check_range(address, length, allow) != STATUS_OK) {
        return STATUS_INVALID_ARG;
    }

    words = (const uint32_t *)data;

    IntMasterDisable();
    rc = FlashProgram((uint32_t *)words, address, length / sizeof(uint32_t));
    IntMasterEnable();

    return (rc == 0) ? STATUS_OK : STATUS_FAIL;
}

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t seq;
    uint32_t hdr_crc32;
} nvs_page_hdr_t;

typedef struct {
    uint32_t magic;
    uint32_t crc32;
    uint16_t ns_len;
    uint16_t key_len;
    uint16_t val_len;
    uint16_t reserved;
} nvs_rec_hdr_t;

#define NVS_REC_MAGIC 0x52454331U   /* "REC1" */

static uint32_t s_active_page;

static uint32_t nvs_page_base(uint32_t page_index)
{
    return FLASH_NVS_BASE + (page_index * NVS_PAGE_SIZE);
}

static uint32_t nvs_hdr_crc(const nvs_page_hdr_t *hdr)
{
    uint32_t crc = CRC32_INIT_VALUE;
    crc = crc32_update(crc, &hdr->magic, sizeof(hdr->magic));
    crc = crc32_update(crc, &hdr->version, sizeof(hdr->version));
    crc = crc32_update(crc, &hdr->seq, sizeof(hdr->seq));
    return crc32_finalize(crc);
}

static int nvs_page_hdr_valid(const nvs_page_hdr_t *hdr)
{
    if (hdr->magic != NVS_PAGE_MAGIC) {
        return 0;
    }
    if (hdr->version != NVS_PAGE_VERSION) {
        return 0;
    }
    if (hdr->hdr_crc32 != nvs_hdr_crc(hdr)) {
        return 0;
    }
    return 1;
}

static int nvs_page_erased(const nvs_page_hdr_t *hdr)
{
    const uint8_t *p = (const uint8_t *)hdr;
    uint32_t i;

    for (i = 0U; i < sizeof(nvs_page_hdr_t); i++) {
        if (p[i] != 0xFFU) {
            return 0;
        }
    }
    return 1;
}

static uint32_t nvs_align4(uint32_t v)
{
    return (v + 3U) & ~3U;
}

static status_t nvs_program_words(uint32_t address, const void *data, uint32_t len)
{
    return nvs_flash_program(address, data, len, NVS_FLASH_ALLOW_NVS);
}

static status_t nvs_erase_page(uint32_t page_index)
{
    return nvs_flash_erase(nvs_page_base(page_index), NVS_PAGE_SIZE, NVS_FLASH_ALLOW_NVS);
}

static status_t nvs_write_page_header(uint32_t page_index, uint32_t seq)
{
    nvs_page_hdr_t hdr;

    hdr.magic = NVS_PAGE_MAGIC;
    hdr.version = NVS_PAGE_VERSION;
    hdr.seq = seq;
    hdr.hdr_crc32 = nvs_hdr_crc(&hdr);

    return nvs_program_words(nvs_page_base(page_index), &hdr, sizeof(hdr));
}

static status_t nvs_scan_pages(void)
{
    uint32_t i;
    uint32_t best = UINT32_MAX;
    int found = 0;

    for (i = 0U; i < NVS_PAGE_COUNT; i++) {
        const nvs_page_hdr_t *hdr = (const nvs_page_hdr_t *)nvs_page_base(i);

        if (nvs_page_erased(hdr)) {
            continue;
        }
        if (nvs_page_hdr_valid(hdr) == 0) {
            continue;
        }
        if (hdr->seq >= best) {
            best = hdr->seq;
            s_active_page = i;
            found = 1;
        }
    }

    if (found == 0) {
        s_active_page = 0U;
        if (nvs_erase_page(0U) != STATUS_OK) {
            return STATUS_FAIL;
        }
        if (nvs_write_page_header(0U, 0U) != STATUS_OK) {
            return STATUS_FAIL;
        }
    }

    return STATUS_OK;
}

status_t nvs_init(void)
{
    s_active_page = 0U;
    return nvs_scan_pages();
}

uint32_t nvs_active_page_index(void)
{
    return s_active_page;
}

void nvs_set_active_page_index(uint32_t page_index)
{
    if (page_index < NVS_PAGE_COUNT) {
        s_active_page = page_index;
    }
}

static status_t nvs_find_record(const char *ns, const char *key, uint32_t *offset_out,
                                nvs_rec_hdr_t *hdr_out)
{
    uint32_t offset = NVS_DATA_OFFSET;
    const uint8_t *page = (const uint8_t *)nvs_page_base(s_active_page);
    size_t ns_len = strlen(ns);
    size_t key_len = strlen(key);

    if ((ns_len == 0U) || (ns_len >= NVS_NS_MAX) || (key_len == 0U) || (key_len >= NVS_KEY_MAX)) {
        return STATUS_INVALID_ARG;
    }

    while ((offset + sizeof(nvs_rec_hdr_t)) <= NVS_PAGE_SIZE) {
        const nvs_rec_hdr_t *rh = (const nvs_rec_hdr_t *)(page + offset);
        uint32_t rec_total;

        if (rh->magic == 0xFFFFFFFFU) {
            break;
        }
        if (rh->magic != NVS_REC_MAGIC) {
            return STATUS_FAIL;
        }

        rec_total = sizeof(nvs_rec_hdr_t) + nvs_align4(rh->ns_len) + nvs_align4(rh->key_len) +
                    nvs_align4(rh->val_len);
        if ((offset + rec_total) > NVS_PAGE_SIZE) {
            return STATUS_FAIL;
        }

        if ((rh->ns_len == ns_len) && (rh->key_len == key_len)) {
            const char *rec_ns = (const char *)(page + offset + sizeof(nvs_rec_hdr_t));
            const char *rec_key = rec_ns + nvs_align4(rh->ns_len);

            if ((strncmp(rec_ns, ns, ns_len) == 0) && (strncmp(rec_key, key, key_len) == 0)) {
                if (offset_out != NULL) {
                    *offset_out = offset;
                }
                if (hdr_out != NULL) {
                    *hdr_out = *rh;
                }
                return STATUS_OK;
            }
        }

        offset += rec_total;
    }

    return STATUS_FAIL;
}

static status_t nvs_build_page_payload(uint8_t *buf, uint32_t buf_size, uint32_t *out_len,
                                       const char *ns, const char *key, const void *val,
                                       uint32_t val_len, int replace)
{
    uint32_t dst = 0U;
    uint32_t src = NVS_DATA_OFFSET;
    const uint8_t *page = (const uint8_t *)nvs_page_base(s_active_page);
    size_t ns_len = strlen(ns);
    size_t key_len = strlen(key);

    if ((ns_len == 0U) || (ns_len >= NVS_NS_MAX) || (key_len == 0U) || (key_len >= NVS_KEY_MAX) ||
        (val_len > NVS_BLOB_MAX)) {
        return STATUS_INVALID_ARG;
    }

    while (src < NVS_PAGE_SIZE) {
        const nvs_rec_hdr_t *rh = (const nvs_rec_hdr_t *)(page + src);
        uint32_t rec_total;

        if (rh->magic == 0xFFFFFFFFU) {
            break;
        }
        if (rh->magic != NVS_REC_MAGIC) {
            return STATUS_FAIL;
        }

        rec_total = sizeof(nvs_rec_hdr_t) + nvs_align4(rh->ns_len) + nvs_align4(rh->key_len) +
                    nvs_align4(rh->val_len);
        if ((src + rec_total) > NVS_PAGE_SIZE) {
            return STATUS_FAIL;
        }

        if (replace != 0) {
            const char *rec_ns = (const char *)(page + src + sizeof(nvs_rec_hdr_t));
            const char *rec_key = rec_ns + nvs_align4(rh->ns_len);

            if ((rh->ns_len == ns_len) && (rh->key_len == key_len) &&
                (strncmp(rec_ns, ns, ns_len) == 0) && (strncmp(rec_key, key, key_len) == 0)) {
                src += rec_total;
                continue;
            }
        }

        if ((dst + rec_total) > buf_size) {
            return STATUS_NO_MEM;
        }
        memcpy(buf + dst, page + src, rec_total);
        dst += rec_total;
        src += rec_total;
    }

    {
        nvs_rec_hdr_t rh;
        uint32_t rec_total = (uint32_t)(sizeof(nvs_rec_hdr_t) + nvs_align4((uint32_t)ns_len) +
                                        nvs_align4((uint32_t)key_len) + nvs_align4(val_len));

        if ((dst + rec_total) > buf_size) {
            return STATUS_NO_MEM;
        }

        rh.magic = NVS_REC_MAGIC;
        rh.ns_len = (uint16_t)ns_len;
        rh.key_len = (uint16_t)key_len;
        rh.val_len = (uint16_t)val_len;
        rh.reserved = 0U;
        rh.crc32 = 0U;

        memcpy(buf + dst, &rh, sizeof(rh));
        dst += sizeof(rh);
        memcpy(buf + dst, ns, ns_len);
        dst += nvs_align4((uint32_t)ns_len);
        memcpy(buf + dst, key, key_len);
        dst += nvs_align4((uint32_t)key_len);
        if (val_len > 0U) {
            memcpy(buf + dst, val, val_len);
            dst += nvs_align4(val_len);
        }

        {
            uint32_t rec_base = dst - rec_total;
            uint32_t crc = crc32_compute(buf + rec_base, rec_total);
            memcpy(buf + rec_base + offsetof(nvs_rec_hdr_t, crc32), &crc, sizeof(crc));
        }
    }

    *out_len = dst;
    return STATUS_OK;
}

static status_t nvs_put_record(const char *ns, const char *key, const void *val, uint32_t val_len,
                               int replace)
{
    uint8_t payload[NVS_PAGE_SIZE - NVS_PAGE_HDR_SIZE];
    uint32_t payload_len = 0U;
    uint32_t inactive;
    uint32_t next_seq;
    const nvs_page_hdr_t *active_hdr;
    status_t st;

    st = nvs_build_page_payload(payload, sizeof(payload), &payload_len, ns, key, val, val_len,
                                replace);
    if (st != STATUS_OK) {
        return st;
    }

    inactive = (s_active_page + 1U) % NVS_PAGE_COUNT;
    active_hdr = (const nvs_page_hdr_t *)nvs_page_base(s_active_page);
    next_seq = nvs_page_hdr_valid(active_hdr) ? (active_hdr->seq + 1U) : 0U;

    st = nvs_erase_page(inactive);
    if (st != STATUS_OK) {
        return st;
    }

    st = nvs_write_page_header(inactive, next_seq);
    if (st != STATUS_OK) {
        return st;
    }

    /* 保留 ota_meta 区（前 256 B 数据区含 ota_meta 位于 NVS_OTA_META_OFFSET） */
    {
        const uint8_t *active = (const uint8_t *)nvs_page_base(s_active_page);
        st = nvs_program_words(nvs_page_base(inactive) + NVS_OTA_META_OFFSET,
                               active + NVS_OTA_META_OFFSET, NVS_OTA_META_SIZE);
        if (st != STATUS_OK) {
            return st;
        }
    }

    st = nvs_program_words(nvs_page_base(inactive) + NVS_DATA_OFFSET,
                           payload, payload_len);
    if (st != STATUS_OK) {
        return st;
    }

    s_active_page = inactive;
    return STATUS_OK;
}

status_t nvs_set_blob(const char *ns, const char *key, const void *data, uint32_t len)
{
    nvs_rec_hdr_t existing;
    int replace = (nvs_find_record(ns, key, NULL, &existing) == STATUS_OK) ? 1 : 0;

    if ((data == NULL) && (len > 0U)) {
        return STATUS_INVALID_ARG;
    }

    return nvs_put_record(ns, key, data, len, replace);
}

status_t nvs_get_blob(const char *ns, const char *key, void *data, uint32_t *len_inout)
{
    nvs_rec_hdr_t rh;
    uint32_t offset;
    const uint8_t *page;
    const uint8_t *val;

    if ((key == NULL) || (len_inout == NULL)) {
        return STATUS_INVALID_ARG;
    }

    if (nvs_find_record(ns, key, &offset, &rh) != STATUS_OK) {
        return STATUS_FAIL;
    }

    page = (const uint8_t *)nvs_page_base(s_active_page);
    val = page + offset + sizeof(nvs_rec_hdr_t) + nvs_align4(rh.ns_len) + nvs_align4(rh.key_len);

    if (*len_inout < rh.val_len) {
        *len_inout = rh.val_len;
        return STATUS_NO_MEM;
    }

    if ((data != NULL) && (rh.val_len > 0U)) {
        memcpy(data, val, rh.val_len);
    }
    *len_inout = rh.val_len;
    return STATUS_OK;
}

status_t nvs_set_u32(const char *ns, const char *key, uint32_t value)
{
    return nvs_set_blob(ns, key, &value, sizeof(value));
}

status_t nvs_get_u32(const char *ns, const char *key, uint32_t *value)
{
    uint32_t len = sizeof(*value);

    if (value == NULL) {
        return STATUS_INVALID_ARG;
    }

    return nvs_get_blob(ns, key, value, &len);
}
