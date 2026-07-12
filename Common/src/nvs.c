/**
 * @file    nvs.c
 * @brief   NVS 页式 KV、配置缓存与参数访问策略
 */

#include "nvs.h"

#include "device_profile.h"
#include "encoder_polarity.h"
#include "crc32.h"
#include "flash_layout.h"

#include "driverlib/flash.h"
#include "driverlib/interrupt.h"

#include "inc/hw_memmap.h"
#include "inc/hw_sysctl.h"
#include "inc/hw_types.h"

#if defined(NVS_RTOS_LOCK)
#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

#include "bsp_uart.h"
#endif

#include <stddef.h>
#include <string.h>

/* -------------------------------------------------------------------------- */
/* 页 / 记录内部结构                                                            */
/* -------------------------------------------------------------------------- */

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

#define NVS_SRC_FACTORY   (1U << NVS_WRITE_SRC_FACTORY)
#define NVS_SRC_CMD        (1U << NVS_WRITE_SRC_CMD)
#define NVS_SRC_PROTOCOL   (1U << NVS_WRITE_SRC_PROTOCOL)
#define NVS_SRC_INTERNAL   (1U << NVS_WRITE_SRC_INTERNAL)

typedef struct {
    u8_t persist;
    u8_t write_mask;
} nvs_param_policy_t;

static uint32_t s_active_page;
static nvs_cfg_t s_cfg;
static int s_first_boot;
static uint8_t s_nvs_payload[NVS_PAGE_SIZE - NVS_PAGE_HDR_SIZE] __attribute__((aligned(4)));
static uint8_t s_boot_rsvd_copy[NVS_BOOT_RSVD_SIZE] __attribute__((aligned(4)));
static int s_boot_rsvd_override;

#if defined(NVS_RTOS_LOCK)
static SemaphoreHandle_t s_nvs_mu;
#endif

static void nvs_lock(void)
{
#if defined(NVS_RTOS_LOCK)
    if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
        if (s_nvs_mu == NULL) {
            s_nvs_mu = xSemaphoreCreateMutex();
            if (s_nvs_mu == NULL) {
                bsp_uart_debug_puts("[nvs] mutex create failed\r\n");
            }
        }
        if (s_nvs_mu != NULL) {
            (void)xSemaphoreTake(s_nvs_mu, portMAX_DELAY);
        }
    }
#else
    /* no-op */
#endif
}

static void nvs_unlock(void)
{
#if defined(NVS_RTOS_LOCK)
    if ((s_nvs_mu != NULL) && (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING)) {
        (void)xSemaphoreGive(s_nvs_mu);
    }
#else
    /* no-op */
#endif
}

static status_t nvs_param_persist_u32(nvs_param_id_t id, const char *ns, const char *key, u32_t value);
static status_t nvs_get_u32_impl(const char *ns, const char *key, u32_t *value);
static void nvs_cfg_apply_defaults(nvs_cfg_t *cfg);
static void nvs_cfg_reconcile_encoder_dir(nvs_cfg_t *cfg);
static void nvs_cfg_reconcile_motor_dir(nvs_cfg_t *cfg);
static void nvs_cfg_load_from_flash(nvs_cfg_t *cfg);
static void nvs_cfg_detect_first_boot(void);
static status_t nvs_cfg_seed_if_needed(void);
static void nvs_flash_scheduler_guard_begin(int *suspended);
static void nvs_flash_scheduler_guard_end(int suspended);
static void nvs_cfg_ensure_serial(void);
static int nvs_load_blob_exact(const char *ns, const char *key, void *dst, u32_t expect_len);
static void nvs_dev_fill_serial(char *serial, size_t size);

static const nvs_param_policy_t s_param_policy[NVS_PARAM_COUNT] = {
    [NVS_PARAM_SCHEMA] = {1U, NVS_SRC_INTERNAL},
    [NVS_PARAM_SERIAL] = {1U, NVS_SRC_FACTORY},
    [NVS_PARAM_HW_REV] = {1U, NVS_SRC_FACTORY},
    [NVS_PARAM_BOOT_COUNT] = {1U, NVS_SRC_INTERNAL},
    [NVS_PARAM_FW_VERSION] = {1U, NVS_SRC_FACTORY | NVS_SRC_CMD | NVS_SRC_PROTOCOL | NVS_SRC_INTERNAL},
    [NVS_PARAM_PID_SPEED] = {0U, NVS_SRC_CMD | NVS_SRC_PROTOCOL},
    [NVS_PARAM_PID_LINE] = {0U, NVS_SRC_CMD | NVS_SRC_PROTOCOL},
    [NVS_PARAM_SPD_LIMIT] = {1U, NVS_SRC_FACTORY | NVS_SRC_CMD | NVS_SRC_PROTOCOL},
    [NVS_PARAM_KINEMATICS] = {1U, NVS_SRC_FACTORY | NVS_SRC_CMD | NVS_SRC_PROTOCOL},
    [NVS_PARAM_MOTOR_DIR] = {1U, NVS_SRC_FACTORY | NVS_SRC_CMD | NVS_SRC_PROTOCOL},
    [NVS_PARAM_IMU_OFFSET] = {1U, NVS_SRC_FACTORY | NVS_SRC_PROTOCOL},
    [NVS_PARAM_LINE_THRESHOLD] = {1U, NVS_SRC_FACTORY | NVS_SRC_PROTOCOL},
    [NVS_PARAM_ENCODER_ZERO] = {1U, NVS_SRC_FACTORY | NVS_SRC_PROTOCOL},
    [NVS_PARAM_BATTERY_CAL] = {1U, NVS_SRC_FACTORY | NVS_SRC_PROTOCOL},
    [NVS_PARAM_LAST_MODE] = {1U, NVS_SRC_INTERNAL},
    [NVS_PARAM_ENCODER_DIR] = {1U, NVS_SRC_FACTORY | NVS_SRC_CMD | NVS_SRC_PROTOCOL},
};

/* -------------------------------------------------------------------------- */
/* Flash 范围检查                                                             */
/* -------------------------------------------------------------------------- */

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

    return STATUS_INVALID_ARG;
}

static void nvs_flash_scheduler_guard_begin(int *suspended)
{
    *suspended = 0;

#if defined(NVS_RTOS_LOCK)
    if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
        vTaskSuspendAll();
        *suspended = 1;
    }
#else
    (void)suspended;
#endif
}

static void nvs_flash_scheduler_guard_end(int suspended)
{
#if defined(NVS_RTOS_LOCK)
    if (suspended != 0) {
        (void)xTaskResumeAll();
    }
#else
    (void)suspended;
#endif
}

status_t nvs_flash_erase(uint32_t address, uint32_t length, nvs_flash_allow_t allow)
{
    uint32_t offset;
    int32_t rc;
    int suspended;

    if (nvs_flash_check_range(address, length, allow) != STATUS_OK) {
        return STATUS_INVALID_ARG;
    }

    nvs_flash_scheduler_guard_begin(&suspended);

    for (offset = 0U; offset < length; offset += FLASH_ERASE_BLOCK_SIZE) {
        uint32_t block_addr = address + offset;

        IntMasterDisable();
        rc = FlashErase(block_addr);
        IntMasterEnable();

        if (rc != 0) {
            nvs_flash_scheduler_guard_end(suspended);
            return STATUS_FAIL;
        }
    }

    nvs_flash_scheduler_guard_end(suspended);
    return STATUS_OK;
}

status_t nvs_flash_program(uint32_t address, const void *data, uint32_t length,
                           nvs_flash_allow_t allow)
{
    int32_t rc;
    const uint32_t *words;
    int suspended;

    if ((data == NULL) || (length == 0U)) {
        return STATUS_INVALID_ARG;
    }

    if (nvs_flash_check_range(address, length, allow) != STATUS_OK) {
        return STATUS_INVALID_ARG;
    }

    if ((length % FLASH_PROGRAM_ALIGN) != 0U) {
        return STATUS_INVALID_ARG;
    }

    words = (const uint32_t *)data;

    nvs_flash_scheduler_guard_begin(&suspended);

    IntMasterDisable();
    rc = FlashProgram((uint32_t *)words, address, length);
    IntMasterEnable();

    nvs_flash_scheduler_guard_end(suspended);

    return (rc == 0) ? STATUS_OK : STATUS_FAIL;
}

/* -------------------------------------------------------------------------- */
/* 页式 KV                                                                     */
/* -------------------------------------------------------------------------- */

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

static uint32_t nvs_record_total_len(const nvs_rec_hdr_t *rh)
{
    return sizeof(nvs_rec_hdr_t) + nvs_align4(rh->ns_len) + nvs_align4(rh->key_len) +
           nvs_align4(rh->val_len);
}

static int nvs_record_crc_valid(const uint8_t *rec_base, uint32_t rec_total)
{
    uint8_t scratch[256U];
    nvs_rec_hdr_t *rh;
    uint32_t stored_crc;
    uint32_t computed_crc;

    if (rec_total > sizeof(scratch)) {
        return 0;
    }

    (void)memcpy(scratch, rec_base, rec_total);
    rh = (nvs_rec_hdr_t *)scratch;
    stored_crc = rh->crc32;
    rh->crc32 = 0U;
    computed_crc = crc32_compute(scratch, rec_total);

    return (computed_crc == stored_crc) ? 1 : 0;
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
    uint32_t best = 0U;
    int found = 0;

    for (i = 0U; i < NVS_PAGE_COUNT; i++) {
        const nvs_page_hdr_t *hdr = (const nvs_page_hdr_t *)nvs_page_base(i);

        if (nvs_page_erased(hdr)) {
            continue;
        }
        if (nvs_page_hdr_valid(hdr) == 0) {
            continue;
        }
        if ((found == 0) || (hdr->seq >= best)) {
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

        rec_total = nvs_record_total_len(rh);
        if ((offset + rec_total) > NVS_PAGE_SIZE) {
            return STATUS_FAIL;
        }

        if (nvs_record_crc_valid(page + offset, rec_total) == 0) {
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

        rec_total = nvs_record_total_len(rh);
        if ((src + rec_total) > NVS_PAGE_SIZE) {
            return STATUS_FAIL;
        }

        if (nvs_record_crc_valid(page + src, rec_total) == 0) {
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
        {
            uint32_t ns_pad = nvs_align4((uint32_t)ns_len);
            if (ns_pad > (uint32_t)ns_len) {
                (void)memset(buf + dst + ns_len, 0, ns_pad - (uint32_t)ns_len);
            }
            dst += ns_pad;
        }
        memcpy(buf + dst, key, key_len);
        {
            uint32_t key_pad = nvs_align4((uint32_t)key_len);
            if (key_pad > (uint32_t)key_len) {
                (void)memset(buf + dst + key_len, 0, key_pad - (uint32_t)key_len);
            }
            dst += key_pad;
        }
        {
            uint32_t val_base = dst;

            if (val_len > 0U) {
                memcpy(buf + dst, val, val_len);
            }
            {
                uint32_t val_pad = nvs_align4(val_len);
                if (val_pad > val_len) {
                    (void)memset(buf + val_base + val_len, 0, val_pad - val_len);
                }
                dst = val_base + val_pad;
            }
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

static status_t nvs_append_record(uint8_t *buf, uint32_t buf_size, uint32_t *dst, const char *ns,
                                  const char *key, const void *val, uint32_t val_len)
{
    size_t ns_len = strlen(ns);
    size_t key_len = strlen(key);
    nvs_rec_hdr_t rh;
    uint32_t rec_total;
    uint32_t rec_base;
    uint32_t crc;

    if ((ns_len == 0U) || (ns_len >= NVS_NS_MAX) || (key_len == 0U) || (key_len >= NVS_KEY_MAX) ||
        (val_len > NVS_BLOB_MAX)) {
        return STATUS_INVALID_ARG;
    }

    rec_total = (uint32_t)(sizeof(nvs_rec_hdr_t) + nvs_align4((uint32_t)ns_len) +
                           nvs_align4((uint32_t)key_len) + nvs_align4(val_len));
    if ((*dst + rec_total) > buf_size) {
        return STATUS_NO_MEM;
    }

    rec_base = *dst;
    rh.magic = NVS_REC_MAGIC;
    rh.ns_len = (uint16_t)ns_len;
    rh.key_len = (uint16_t)key_len;
    rh.val_len = (uint16_t)val_len;
    rh.reserved = 0U;
    rh.crc32 = 0U;

    (void)memcpy(buf + *dst, &rh, sizeof(rh));
    *dst += sizeof(rh);
    (void)memcpy(buf + *dst, ns, ns_len);
    {
        uint32_t ns_pad = nvs_align4((uint32_t)ns_len);
        if (ns_pad > (uint32_t)ns_len) {
            (void)memset(buf + *dst + ns_len, 0, ns_pad - (uint32_t)ns_len);
        }
        *dst += ns_pad;
    }
    (void)memcpy(buf + *dst, key, key_len);
    {
        uint32_t key_pad = nvs_align4((uint32_t)key_len);
        if (key_pad > (uint32_t)key_len) {
            (void)memset(buf + *dst + key_len, 0, key_pad - (uint32_t)key_len);
        }
        *dst += key_pad;
    }
    {
        uint32_t val_base = *dst;

        if (val_len > 0U) {
            (void)memcpy(buf + *dst, val, val_len);
        }
        {
            uint32_t val_pad = nvs_align4(val_len);
            if (val_pad > val_len) {
                (void)memset(buf + val_base + val_len, 0, val_pad - val_len);
            }
            *dst = val_base + val_pad;
        }
    }

    crc = crc32_compute(buf + rec_base, rec_total);
    (void)memcpy(buf + rec_base + offsetof(nvs_rec_hdr_t, crc32), &crc, sizeof(crc));
    return STATUS_OK;
}

static status_t nvs_copy_active_kv_payload(uint8_t *buf, uint32_t buf_size, uint32_t *out_len)
{
    uint32_t src = NVS_DATA_OFFSET;
    uint32_t dst = 0U;
    const uint8_t *page = (const uint8_t *)nvs_page_base(s_active_page);

    while (src < NVS_PAGE_SIZE) {
        const nvs_rec_hdr_t *rh = (const nvs_rec_hdr_t *)(page + src);
        uint32_t rec_total;

        if (rh->magic == 0xFFFFFFFFU) {
            break;
        }
        if (rh->magic != NVS_REC_MAGIC) {
            return STATUS_FAIL;
        }

        rec_total = nvs_record_total_len(rh);
        if ((src + rec_total) > NVS_PAGE_SIZE) {
            return STATUS_FAIL;
        }

        if (nvs_record_crc_valid(page + src, rec_total) == 0) {
            return STATUS_FAIL;
        }

        if ((dst + rec_total) > buf_size) {
            return STATUS_NO_MEM;
        }

        (void)memcpy(buf + dst, page + src, rec_total);
        dst += rec_total;
        src += rec_total;
    }

    *out_len = dst;
    return STATUS_OK;
}

static status_t nvs_commit_page_payload(const uint8_t *payload, uint32_t payload_len)
{
    uint32_t inactive;
    uint32_t next_seq;
    const nvs_page_hdr_t *active_hdr;
    status_t st;

    inactive = (s_active_page + 1U) % NVS_PAGE_COUNT;
    active_hdr = (const nvs_page_hdr_t *)nvs_page_base(s_active_page);
    next_seq = nvs_page_hdr_valid(active_hdr) ? (active_hdr->seq + 1U) : 0U;

    st = nvs_erase_page(inactive);
    if (st != STATUS_OK) {
        return st;
    }

    {
        const uint8_t *active = (const uint8_t *)nvs_page_base(s_active_page);

        if (s_boot_rsvd_override == 0) {
            (void)memcpy(s_boot_rsvd_copy, active + NVS_BOOT_RSVD_OFFSET, NVS_BOOT_RSVD_SIZE);
        }
        st = nvs_program_words(nvs_page_base(inactive) + NVS_BOOT_RSVD_OFFSET, s_boot_rsvd_copy,
                               NVS_BOOT_RSVD_SIZE);
        if (st != STATUS_OK) {
            return st;
        }
    }

    st = nvs_program_words(nvs_page_base(inactive) + NVS_DATA_OFFSET, payload, payload_len);
    if (st != STATUS_OK) {
        return st;
    }

    st = nvs_write_page_header(inactive, next_seq);
    if (st != STATUS_OK) {
        return st;
    }

    s_active_page = inactive;
    s_boot_rsvd_override = 0;
    return STATUS_OK;
}

static status_t nvs_put_record(const char *ns, const char *key, const void *val, uint32_t val_len,
                               int replace)
{
    uint32_t payload_len = 0U;
    status_t st;

    st = nvs_build_page_payload(s_nvs_payload, sizeof(s_nvs_payload), &payload_len, ns, key, val,
                                val_len, replace);
    if (st != STATUS_OK) {
        return st;
    }

    return nvs_commit_page_payload(s_nvs_payload, payload_len);
}

static status_t nvs_set_blob_impl(const char *ns, const char *key, const void *data, uint32_t len)
{
    nvs_rec_hdr_t existing;
    int replace = (nvs_find_record(ns, key, NULL, &existing) == STATUS_OK) ? 1 : 0;

    if ((data == NULL) && (len > 0U)) {
        return STATUS_INVALID_ARG;
    }

    return nvs_put_record(ns, key, data, len, replace);
}

static status_t nvs_get_blob_impl(const char *ns, const char *key, void *data, uint32_t *len_inout)
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

status_t nvs_set_blob(const char *ns, const char *key, const void *data, uint32_t len)
{
    status_t st;

    nvs_lock();
    st = nvs_set_blob_impl(ns, key, data, len);
    nvs_unlock();
    return st;
}

status_t nvs_get_blob(const char *ns, const char *key, void *data, uint32_t *len_inout)
{
    status_t st;

    nvs_lock();
    st = nvs_get_blob_impl(ns, key, data, len_inout);
    nvs_unlock();
    return st;
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

status_t nvs_init(void)
{
    status_t st;

    nvs_lock();

    s_active_page = 0U;
    st = nvs_scan_pages();
    if (st != STATUS_OK) {
        nvs_unlock();
        return st;
    }

    nvs_cfg_apply_defaults(&s_cfg);
    nvs_cfg_load_from_flash(&s_cfg);
    nvs_cfg_detect_first_boot();

    if (s_cfg.serial[0] == '\0') {
        nvs_dev_fill_serial(s_cfg.serial, sizeof(s_cfg.serial));
    }

    if (s_cfg.boot_count < UINT32_MAX) {
        s_cfg.boot_count++;
    }

    nvs_unlock();
    return STATUS_OK;
}

status_t nvs_startup_finalize(void)
{
    u32_t schema = 0U;
    status_t st;
    int had_schema = (nvs_get_u32_impl(NVS_CFG_NS_DEV, NVS_CFG_KEY_SCHEMA, &schema) == STATUS_OK);

    if (!had_schema) {
        st = nvs_cfg_seed_if_needed();
        if (st != STATUS_OK) {
            return st;
        }
        nvs_cfg_ensure_serial();
        return STATUS_OK;
    }

    nvs_cfg_ensure_serial();
    return nvs_param_persist_u32(NVS_PARAM_BOOT_COUNT, NVS_CFG_NS_DEV, NVS_CFG_KEY_BOOT_CNT,
                                 s_cfg.boot_count);
}

uint32_t nvs_active_page_index(void)
{
    return s_active_page;
}

void nvs_set_active_page_index(uint32_t page_index)
{
#if defined(NVS_CMD_RAW_KV)
    nvs_lock();
    if (page_index < NVS_PAGE_COUNT) {
        s_active_page = page_index;
    }
    nvs_unlock();
#else
    (void)page_index;
#endif
}

/* -------------------------------------------------------------------------- */
/* 配置默认值 / 加载                                                            */
/* -------------------------------------------------------------------------- */

static void nvs_cfg_apply_defaults(nvs_cfg_t *cfg)
{
    const device_product_profile_t *profile = device_profile_product();
    u32_t i;

    (void)memset(cfg, 0, sizeof(*cfg));

    cfg->schema_version = NVS_CFG_SCHEMA_VERSION;
    cfg->hw_rev = (profile->product_id == DEVICE_PRODUCT_ID_CAR_2WD_FULL)
                      ? NVS_HW_REV_CAR_2WD_V1
                      : NVS_HW_REV_CAR_4WD_V1;

    cfg->pid_speed.kp = 1.0f;
    cfg->pid_speed.ki = 0.20f;
    cfg->pid_speed.kd = 0.0f;
    cfg->pid_line.kp = 2.0f;
    cfg->pid_line.ki = 0.0f;
    cfg->pid_line.kd = 0.1f;

    cfg->spd_limit.max_rpm = 300.0f;
    cfg->spd_limit.max_accel_rpm_s = 600.0f;

    cfg->kinematics.wheel_diam_m = 0.065f;
    cfg->kinematics.gear_ratio = 30.0f;
    cfg->kinematics.encoder_cpr = 11U;
    if (profile->product_id == DEVICE_PRODUCT_ID_CAR_2WD_FULL) {
        cfg->kinematics.track_width_m = 0.0f;
        cfg->kinematics.wheelbase_m = 0.16f;
    } else {
        cfg->kinematics.track_width_m = 0.18f;
        cfg->kinematics.wheelbase_m = 0.16f;
    }

    cfg->battery_cal.scale = 1.0f;
    cfg->battery_cal.offset_v = 0.0f;

    for (i = 0U; i < NVS_CFG_LINE_SENSOR_COUNT; i++) {
        cfg->line_threshold.threshold[i] = 2048U;
    }

    cfg->last_mode = NVS_RUN_MODE_IDLE;

    /* 出厂 encoder_dir_mask 由 encoder_polarity_board.h / encoder_polarity.h 决定 */
    if (profile->product_id != DEVICE_PRODUCT_ID_CAR_2WD_FULL) {
        cfg->encoder_dir_mask = encoder_polarity_default_mask();
    }
}

static void nvs_cfg_reconcile_encoder_dir(nvs_cfg_t *cfg)
{
    const device_product_profile_t *profile = device_profile_product();
    u32_t expected;

    if (cfg == NULL) {
        return;
    }
    if (profile->product_id == DEVICE_PRODUCT_ID_CAR_2WD_FULL) {
        return;
    }

    expected = encoder_polarity_default_mask();
    /*
     * M1/M2 左右轮以板级宏为准；保留 M3/M4 用户位。
     * 清理旧固件误将 M1 写入 encoder_dir_mask 的情况（如 0x03 → 0x02）。
     */
    cfg->encoder_dir_mask = (cfg->encoder_dir_mask & 0x0CU) | expected;
}

static void nvs_cfg_reconcile_motor_dir(nvs_cfg_t *cfg)
{
    const device_product_profile_t *profile = device_profile_product();
    u32_t expected;

    if (cfg == NULL) {
        return;
    }
    if (profile->product_id == DEVICE_PRODUCT_ID_CAR_2WD_FULL) {
        return;
    }

    expected = motor_polarity_default_mask();
    cfg->motor_dir_mask = (cfg->motor_dir_mask & ~0x03U) | (expected & 0x03U) |
                          (cfg->motor_dir_mask & 0x0CU);
}

static int nvs_load_blob_exact(const char *ns, const char *key, void *dst, u32_t expect_len)
{
    u32_t len = expect_len;

    if (nvs_get_blob_impl(ns, key, dst, &len) != STATUS_OK) {
        return 0;
    }
    if (len != expect_len) {
        return 0;
    }
    return 1;
}

static status_t nvs_get_u32_impl(const char *ns, const char *key, u32_t *value)
{
    u32_t len = sizeof(*value);

    if (value == NULL) {
        return STATUS_INVALID_ARG;
    }

    return nvs_get_blob_impl(ns, key, value, &len);
}

static status_t nvs_cfg_persist_defaults(void)
{
    uint32_t len = 0U;
    size_t serial_len;
    status_t st;

    nvs_dev_fill_serial(s_cfg.serial, sizeof(s_cfg.serial));
    serial_len = strlen(s_cfg.serial);

#define NVS_APPEND_U32(ns, key, value)                                              \
    do {                                                                            \
        u32_t _v = (value);                                                         \
        st = nvs_append_record(s_nvs_payload, sizeof(s_nvs_payload), &len, (ns),    \
                               (key), &_v, (u32_t)sizeof(_v));                      \
        if (st != STATUS_OK) {                                                      \
            return st;                                                              \
        }                                                                           \
    } while (0)

#define NVS_APPEND_BLOB(ns, key, ptr, size)                                         \
    do {                                                                            \
        st = nvs_append_record(s_nvs_payload, sizeof(s_nvs_payload), &len, (ns),    \
                               (key), (ptr), (u32_t)(size));                        \
        if (st != STATUS_OK) {                                                      \
            return st;                                                              \
        }                                                                           \
    } while (0)

    NVS_APPEND_U32(NVS_CFG_NS_DEV, NVS_CFG_KEY_SCHEMA, s_cfg.schema_version);
    NVS_APPEND_U32(NVS_CFG_NS_DEV, NVS_CFG_KEY_HW_REV, s_cfg.hw_rev);
    NVS_APPEND_U32(NVS_CFG_NS_DEV, NVS_CFG_KEY_BOOT_CNT, s_cfg.boot_count);
    if (serial_len > 0U) {
        NVS_APPEND_BLOB(NVS_CFG_NS_DEV, NVS_CFG_KEY_SERIAL, s_cfg.serial, serial_len);
    }
    NVS_APPEND_BLOB(NVS_CFG_NS_CTRL, NVS_CFG_KEY_SPD_LIM, &s_cfg.spd_limit,
                    sizeof(s_cfg.spd_limit));
    NVS_APPEND_BLOB(NVS_CFG_NS_CTRL, NVS_CFG_KEY_KINEM, &s_cfg.kinematics,
                    sizeof(s_cfg.kinematics));
    NVS_APPEND_U32(NVS_CFG_NS_CTRL, NVS_CFG_KEY_MOT_DIR, s_cfg.motor_dir_mask);
    NVS_APPEND_U32(NVS_CFG_NS_CTRL, NVS_CFG_KEY_ENC_DIR, s_cfg.encoder_dir_mask);
    NVS_APPEND_BLOB(NVS_CFG_NS_CAL, NVS_CFG_KEY_IMU_OFF, &s_cfg.imu_offset,
                    sizeof(s_cfg.imu_offset));
    NVS_APPEND_BLOB(NVS_CFG_NS_CAL, NVS_CFG_KEY_LINE_TH, &s_cfg.line_threshold,
                    sizeof(s_cfg.line_threshold));
    NVS_APPEND_BLOB(NVS_CFG_NS_CAL, NVS_CFG_KEY_ENC_ZERO, &s_cfg.encoder_zero,
                    sizeof(s_cfg.encoder_zero));
    NVS_APPEND_BLOB(NVS_CFG_NS_CAL, NVS_CFG_KEY_BAT_CAL, &s_cfg.battery_cal,
                    sizeof(s_cfg.battery_cal));
    NVS_APPEND_U32(NVS_CFG_NS_USER, NVS_CFG_KEY_LAST_MODE, (u32_t)s_cfg.last_mode);

#undef NVS_APPEND_U32
#undef NVS_APPEND_BLOB

    return nvs_commit_page_payload(s_nvs_payload, len);
}

static void nvs_dev_fill_serial(char *serial, size_t size)
{
    static const char k_hex[] = "0123456789ABCDEF";
    u32_t did0 = HWREG(SYSCTL_DID0);
    u32_t did1 = HWREG(SYSCTL_DID1);
    char tmp[13];
    u32_t val;
    int i;

    if ((serial == NULL) || (size == 0U)) {
        return;
    }

    val = did1;
    for (i = 7; i >= 0; i--) {
        tmp[i] = k_hex[val & 0xFU];
        val >>= 4;
    }

    val = did0 & 0xFFFFU;
    for (i = 11; i >= 8; i--) {
        tmp[i] = k_hex[val & 0xFU];
        val >>= 4;
    }
    tmp[12] = '\0';

    (void)memcpy(serial, tmp, 13U);
    serial[size - 1U] = '\0';
}

static void nvs_cfg_detect_first_boot(void)
{
    u32_t schema = 0U;

    if (nvs_get_u32_impl(NVS_CFG_NS_DEV, NVS_CFG_KEY_SCHEMA, &schema) != STATUS_OK) {
        s_first_boot = 1;
    }
}

static void nvs_cfg_ensure_serial(void)
{
    size_t len;

    if (s_cfg.serial[0] != '\0') {
        return;
    }

    nvs_dev_fill_serial(s_cfg.serial, sizeof(s_cfg.serial));
    len = strlen(s_cfg.serial);
    if (len == 0U) {
        return;
    }

    (void)nvs_set_blob_impl(NVS_CFG_NS_DEV, NVS_CFG_KEY_SERIAL, s_cfg.serial, (u32_t)len);
}

static status_t nvs_cfg_seed_if_needed(void)
{
    u32_t schema = 0U;
    status_t st;

    if (nvs_get_u32_impl(NVS_CFG_NS_DEV, NVS_CFG_KEY_SCHEMA, &schema) == STATUS_OK) {
        return STATUS_OK;
    }

    s_first_boot = 1;
    st = nvs_cfg_persist_defaults();
    if (st != STATUS_OK) {
        return st;
    }

    return STATUS_OK;
}

static void nvs_cfg_load_from_flash(nvs_cfg_t *cfg)
{
    u32_t u32_val;
    char serial[NVS_CFG_SERIAL_MAX];
    u32_t serial_len = sizeof(serial);

    if (nvs_get_u32_impl(NVS_CFG_NS_DEV, NVS_CFG_KEY_SCHEMA, &u32_val) == STATUS_OK) {
        cfg->schema_version = u32_val;
    }

    if (nvs_get_blob_impl(NVS_CFG_NS_DEV, NVS_CFG_KEY_SERIAL, serial, &serial_len) == STATUS_OK) {
        if ((serial_len > 0U) && (serial_len < NVS_CFG_SERIAL_MAX)) {
            (void)memcpy(cfg->serial, serial, serial_len);
            cfg->serial[serial_len] = '\0';
        }
    }

    if (nvs_get_u32_impl(NVS_CFG_NS_DEV, NVS_CFG_KEY_HW_REV, &u32_val) == STATUS_OK) {
        cfg->hw_rev = u32_val;
    }

    if (nvs_get_u32_impl(NVS_CFG_NS_DEV, NVS_CFG_KEY_BOOT_CNT, &u32_val) == STATUS_OK) {
        cfg->boot_count = u32_val;
    }

    {
        char fw_ver[NVS_CFG_FW_VER_MAX];
        u32_t fw_ver_len = sizeof(fw_ver);

        if (nvs_get_blob_impl(NVS_CFG_NS_DEV, NVS_CFG_KEY_FW_VER, fw_ver, &fw_ver_len) == STATUS_OK) {
            if ((fw_ver_len > 0U) && (fw_ver_len < NVS_CFG_FW_VER_MAX)) {
                (void)memcpy(cfg->fw_version, fw_ver, fw_ver_len);
                cfg->fw_version[fw_ver_len] = '\0';
            }
        }
    }

    if (nvs_load_blob_exact(NVS_CFG_NS_CTRL, NVS_CFG_KEY_SPD_LIM, &cfg->spd_limit,
                            (u32_t)sizeof(cfg->spd_limit)) == 0) {
        /* keep default */
    }

    if (nvs_load_blob_exact(NVS_CFG_NS_CTRL, NVS_CFG_KEY_KINEM, &cfg->kinematics,
                            (u32_t)sizeof(cfg->kinematics)) == 0) {
        /* keep default */
    }

    if (nvs_get_u32_impl(NVS_CFG_NS_CTRL, NVS_CFG_KEY_MOT_DIR, &u32_val) == STATUS_OK) {
        cfg->motor_dir_mask = u32_val;
    }

    if (nvs_get_u32_impl(NVS_CFG_NS_CTRL, NVS_CFG_KEY_ENC_DIR, &u32_val) == STATUS_OK) {
        cfg->encoder_dir_mask = u32_val;
    }

    nvs_cfg_reconcile_motor_dir(cfg);
    nvs_cfg_reconcile_encoder_dir(cfg);

    if (nvs_load_blob_exact(NVS_CFG_NS_CAL, NVS_CFG_KEY_IMU_OFF, &cfg->imu_offset,
                            (u32_t)sizeof(cfg->imu_offset)) == 0) {
        /* keep default */
    }

    if (nvs_load_blob_exact(NVS_CFG_NS_CAL, NVS_CFG_KEY_LINE_TH, &cfg->line_threshold,
                            (u32_t)sizeof(cfg->line_threshold)) == 0) {
        /* keep default */
    }

    if (nvs_load_blob_exact(NVS_CFG_NS_CAL, NVS_CFG_KEY_ENC_ZERO, &cfg->encoder_zero,
                            (u32_t)sizeof(cfg->encoder_zero)) == 0) {
        /* keep default */
    }

    if (nvs_load_blob_exact(NVS_CFG_NS_CAL, NVS_CFG_KEY_BAT_CAL, &cfg->battery_cal,
                            (u32_t)sizeof(cfg->battery_cal)) == 0) {
        /* keep default */
    }

    if (nvs_get_u32_impl(NVS_CFG_NS_USER, NVS_CFG_KEY_LAST_MODE, &u32_val) == STATUS_OK) {
        if (u32_val <= (u32_t)NVS_RUN_MODE_REMOTE) {
            cfg->last_mode = (nvs_run_mode_t)u32_val;
        }
    }
}

const nvs_cfg_t *nvs_cfg_get(void)
{
    return &s_cfg;
}

bool nvs_first_boot(void)
{
    return (s_first_boot != 0);
}

/* -------------------------------------------------------------------------- */
/* 参数访问策略                                                                 */
/* -------------------------------------------------------------------------- */

static status_t nvs_param_check_write(nvs_param_id_t id, nvs_write_src_t src)
{
    u8_t bit;

    if (id >= NVS_PARAM_COUNT) {
        return STATUS_INVALID_ARG;
    }
    if (src > NVS_WRITE_SRC_INTERNAL) {
        return STATUS_INVALID_ARG;
    }

    bit = (u8_t)(1U << (u32_t)src);
    if ((s_param_policy[id].write_mask & bit) == 0U) {
        return STATUS_NOT_SUPPORTED;
    }

    return STATUS_OK;
}

bool nvs_param_write_allowed(nvs_param_id_t id, nvs_write_src_t src)
{
    return (nvs_param_check_write(id, src) == STATUS_OK);
}

static status_t nvs_param_persist_blob(nvs_param_id_t id, const char *ns, const char *key,
                                       const void *data, u32_t len)
{
    status_t st;

    if (!s_param_policy[id].persist) {
        return STATUS_OK;
    }

    nvs_lock();
    st = nvs_set_blob_impl(ns, key, data, len);
    nvs_unlock();
    return st;
}

static status_t nvs_param_persist_u32(nvs_param_id_t id, const char *ns, const char *key, u32_t value)
{
    status_t st;

    if (!s_param_policy[id].persist) {
        return STATUS_OK;
    }

    nvs_lock();
    st = nvs_set_blob_impl(ns, key, &value, (u32_t)sizeof(value));
    nvs_unlock();
    return st;
}

status_t nvs_param_set_serial(const char *serial, nvs_write_src_t src)
{
    size_t len;
    status_t st;

    st = nvs_param_check_write(NVS_PARAM_SERIAL, src);
    if (st != STATUS_OK) {
        return st;
    }
    if (serial == NULL) {
        return STATUS_INVALID_ARG;
    }

    len = strlen(serial);
    if ((len == 0U) || (len >= NVS_CFG_SERIAL_MAX)) {
        return STATUS_INVALID_ARG;
    }

    (void)memcpy(s_cfg.serial, serial, len + 1U);
    return nvs_param_persist_blob(NVS_PARAM_SERIAL, NVS_CFG_NS_DEV, NVS_CFG_KEY_SERIAL, serial,
                                  (u32_t)len);
}

status_t nvs_param_set_hw_rev(u32_t hw_rev, nvs_write_src_t src)
{
    status_t st;

    st = nvs_param_check_write(NVS_PARAM_HW_REV, src);
    if (st != STATUS_OK) {
        return st;
    }

    s_cfg.hw_rev = hw_rev;
    return nvs_param_persist_u32(NVS_PARAM_HW_REV, NVS_CFG_NS_DEV, NVS_CFG_KEY_HW_REV, hw_rev);
}

status_t nvs_param_set_fw_version(const char *version, nvs_write_src_t src)
{
    size_t len;
    status_t st;

    st = nvs_param_check_write(NVS_PARAM_FW_VERSION, src);
    if (st != STATUS_OK) {
        return st;
    }
    if (version == NULL) {
        return STATUS_INVALID_ARG;
    }

    len = strlen(version);
    if (len >= NVS_CFG_FW_VER_MAX) {
        return STATUS_INVALID_ARG;
    }

    (void)memcpy(s_cfg.fw_version, version, len + 1U);
    if (len == 0U) {
        return nvs_param_persist_blob(NVS_PARAM_FW_VERSION, NVS_CFG_NS_DEV, NVS_CFG_KEY_FW_VER, "",
                                      0U);
    }

    return nvs_param_persist_blob(NVS_PARAM_FW_VERSION, NVS_CFG_NS_DEV, NVS_CFG_KEY_FW_VER, version,
                                  (u32_t)len);
}

status_t nvs_param_inc_boot_count(void)
{
    status_t st;

    st = nvs_param_check_write(NVS_PARAM_BOOT_COUNT, NVS_WRITE_SRC_INTERNAL);
    if (st != STATUS_OK) {
        return st;
    }

    if (s_cfg.boot_count < UINT32_MAX) {
        s_cfg.boot_count++;
    }

    return nvs_param_persist_u32(NVS_PARAM_BOOT_COUNT, NVS_CFG_NS_DEV, NVS_CFG_KEY_BOOT_CNT,
                                 s_cfg.boot_count);
}

status_t nvs_param_set_pid_speed(const nvs_pid3_t *pid, nvs_write_src_t src)
{
    status_t st;

    st = nvs_param_check_write(NVS_PARAM_PID_SPEED, src);
    if (st != STATUS_OK) {
        return st;
    }
    if (pid == NULL) {
        return STATUS_INVALID_ARG;
    }

    s_cfg.pid_speed = *pid;
    return STATUS_OK;
}

status_t nvs_param_set_pid_line(const nvs_pid3_t *pid, nvs_write_src_t src)
{
    status_t st;

    st = nvs_param_check_write(NVS_PARAM_PID_LINE, src);
    if (st != STATUS_OK) {
        return st;
    }
    if (pid == NULL) {
        return STATUS_INVALID_ARG;
    }

    s_cfg.pid_line = *pid;
    return STATUS_OK;
}

status_t nvs_param_set_spd_limit(const nvs_spd_limit_t *limit, nvs_write_src_t src)
{
    status_t st;

    st = nvs_param_check_write(NVS_PARAM_SPD_LIMIT, src);
    if (st != STATUS_OK) {
        return st;
    }
    if (limit == NULL) {
        return STATUS_INVALID_ARG;
    }

    s_cfg.spd_limit = *limit;
    return nvs_param_persist_blob(NVS_PARAM_SPD_LIMIT, NVS_CFG_NS_CTRL, NVS_CFG_KEY_SPD_LIM, limit,
                                  sizeof(*limit));
}

status_t nvs_param_set_kinematics(const nvs_kinematics_t *kinem, nvs_write_src_t src)
{
    status_t st;

    st = nvs_param_check_write(NVS_PARAM_KINEMATICS, src);
    if (st != STATUS_OK) {
        return st;
    }
    if (kinem == NULL) {
        return STATUS_INVALID_ARG;
    }

    s_cfg.kinematics = *kinem;
    return nvs_param_persist_blob(NVS_PARAM_KINEMATICS, NVS_CFG_NS_CTRL, NVS_CFG_KEY_KINEM, kinem,
                                  sizeof(*kinem));
}

status_t nvs_param_set_motor_dir_mask(u32_t mask, nvs_write_src_t src)
{
    status_t st;

    st = nvs_param_check_write(NVS_PARAM_MOTOR_DIR, src);
    if (st != STATUS_OK) {
        return st;
    }

    s_cfg.motor_dir_mask = mask;
    return nvs_param_persist_u32(NVS_PARAM_MOTOR_DIR, NVS_CFG_NS_CTRL, NVS_CFG_KEY_MOT_DIR, mask);
}

status_t nvs_param_set_encoder_dir_mask(u32_t mask, nvs_write_src_t src)
{
    status_t st;

    st = nvs_param_check_write(NVS_PARAM_ENCODER_DIR, src);
    if (st != STATUS_OK) {
        return st;
    }

    s_cfg.encoder_dir_mask = mask & 0x0FU;
    return nvs_param_persist_u32(NVS_PARAM_ENCODER_DIR, NVS_CFG_NS_CTRL,
                                 NVS_CFG_KEY_ENC_DIR, s_cfg.encoder_dir_mask);
}

status_t nvs_param_set_imu_offset(const nvs_imu_offset_t *offset, nvs_write_src_t src)
{
    status_t st;

    st = nvs_param_check_write(NVS_PARAM_IMU_OFFSET, src);
    if (st != STATUS_OK) {
        return st;
    }
    if (offset == NULL) {
        return STATUS_INVALID_ARG;
    }

    s_cfg.imu_offset = *offset;
    return nvs_param_persist_blob(NVS_PARAM_IMU_OFFSET, NVS_CFG_NS_CAL, NVS_CFG_KEY_IMU_OFF, offset,
                                  sizeof(*offset));
}

status_t nvs_param_set_line_threshold(const nvs_line_threshold_t *threshold, nvs_write_src_t src)
{
    status_t st;

    st = nvs_param_check_write(NVS_PARAM_LINE_THRESHOLD, src);
    if (st != STATUS_OK) {
        return st;
    }
    if (threshold == NULL) {
        return STATUS_INVALID_ARG;
    }

    s_cfg.line_threshold = *threshold;
    return nvs_param_persist_blob(NVS_PARAM_LINE_THRESHOLD, NVS_CFG_NS_CAL, NVS_CFG_KEY_LINE_TH,
                                  threshold, sizeof(*threshold));
}

status_t nvs_param_set_encoder_zero(const nvs_encoder_zero_t *zero, nvs_write_src_t src)
{
    status_t st;

    st = nvs_param_check_write(NVS_PARAM_ENCODER_ZERO, src);
    if (st != STATUS_OK) {
        return st;
    }
    if (zero == NULL) {
        return STATUS_INVALID_ARG;
    }

    s_cfg.encoder_zero = *zero;
    return nvs_param_persist_blob(NVS_PARAM_ENCODER_ZERO, NVS_CFG_NS_CAL, NVS_CFG_KEY_ENC_ZERO, zero,
                                  sizeof(*zero));
}

status_t nvs_param_set_battery_cal(const nvs_battery_cal_t *cal, nvs_write_src_t src)
{
    status_t st;

    st = nvs_param_check_write(NVS_PARAM_BATTERY_CAL, src);
    if (st != STATUS_OK) {
        return st;
    }
    if (cal == NULL) {
        return STATUS_INVALID_ARG;
    }

    s_cfg.battery_cal = *cal;
    return nvs_param_persist_blob(NVS_PARAM_BATTERY_CAL, NVS_CFG_NS_CAL, NVS_CFG_KEY_BAT_CAL, cal,
                                  sizeof(*cal));
}

status_t nvs_param_set_last_mode(nvs_run_mode_t mode, nvs_write_src_t src)
{
    status_t st;

    st = nvs_param_check_write(NVS_PARAM_LAST_MODE, src);
    if (st != STATUS_OK) {
        return st;
    }
    if (mode > NVS_RUN_MODE_REMOTE) {
        return STATUS_INVALID_ARG;
    }

    s_cfg.last_mode = mode;
    return nvs_param_persist_u32(NVS_PARAM_LAST_MODE, NVS_CFG_NS_USER, NVS_CFG_KEY_LAST_MODE,
                                 (u32_t)mode);
}

status_t nvs_factory_reset(void)
{
    char serial[NVS_CFG_SERIAL_MAX];
    char fw_version[NVS_CFG_FW_VER_MAX];
    u32_t hw_rev;
    u32_t boot_count;
    status_t st;

    nvs_lock();

    (void)memcpy(serial, s_cfg.serial, sizeof(serial));
    (void)memcpy(fw_version, s_cfg.fw_version, sizeof(fw_version));
    hw_rev = s_cfg.hw_rev;
    boot_count = s_cfg.boot_count;

    nvs_cfg_apply_defaults(&s_cfg);

    (void)memcpy(s_cfg.serial, serial, sizeof(s_cfg.serial));
    (void)memcpy(s_cfg.fw_version, fw_version, sizeof(s_cfg.fw_version));
    s_cfg.hw_rev = hw_rev;
    s_cfg.boot_count = boot_count;

    st = nvs_set_blob_impl(NVS_CFG_NS_CTRL, NVS_CFG_KEY_SPD_LIM, &s_cfg.spd_limit,
                           (u32_t)sizeof(s_cfg.spd_limit));
    if (st != STATUS_OK) {
        goto done;
    }

    st = nvs_set_blob_impl(NVS_CFG_NS_CTRL, NVS_CFG_KEY_KINEM, &s_cfg.kinematics,
                           (u32_t)sizeof(s_cfg.kinematics));
    if (st != STATUS_OK) {
        goto done;
    }

    st = nvs_set_blob_impl(NVS_CFG_NS_CTRL, NVS_CFG_KEY_MOT_DIR, &s_cfg.motor_dir_mask,
                           (u32_t)sizeof(s_cfg.motor_dir_mask));
    if (st != STATUS_OK) {
        goto done;
    }

    st = nvs_set_blob_impl(NVS_CFG_NS_CAL, NVS_CFG_KEY_IMU_OFF, &s_cfg.imu_offset,
                           (u32_t)sizeof(s_cfg.imu_offset));
    if (st != STATUS_OK) {
        goto done;
    }

    st = nvs_set_blob_impl(NVS_CFG_NS_CAL, NVS_CFG_KEY_LINE_TH, &s_cfg.line_threshold,
                           (u32_t)sizeof(s_cfg.line_threshold));
    if (st != STATUS_OK) {
        goto done;
    }

    st = nvs_set_blob_impl(NVS_CFG_NS_CAL, NVS_CFG_KEY_ENC_ZERO, &s_cfg.encoder_zero,
                           (u32_t)sizeof(s_cfg.encoder_zero));
    if (st != STATUS_OK) {
        goto done;
    }

    st = nvs_set_blob_impl(NVS_CFG_NS_CAL, NVS_CFG_KEY_BAT_CAL, &s_cfg.battery_cal,
                           (u32_t)sizeof(s_cfg.battery_cal));
    if (st != STATUS_OK) {
        goto done;
    }

    st = nvs_set_blob_impl(NVS_CFG_NS_USER, NVS_CFG_KEY_LAST_MODE, &s_cfg.last_mode,
                           (u32_t)sizeof(s_cfg.last_mode));

done:
    nvs_unlock();
    return st;
}

status_t nvs_boot_slot_get(uint32_t *slot_out)
{
    if (slot_out == NULL) {
        return STATUS_INVALID_ARG;
    }

    *slot_out = boot_slot_read();
    return STATUS_OK;
}

status_t nvs_boot_slot_set(uint32_t slot)
{
    boot_slot_cfg_t *cfg;
    status_t st;
    uint32_t payload_len = 0U;

    if (slot > BOOT_SLOT_B) {
        return STATUS_INVALID_ARG;
    }

    nvs_lock();

    st = nvs_copy_active_kv_payload(s_nvs_payload, sizeof(s_nvs_payload), &payload_len);
    if (st != STATUS_OK) {
        nvs_unlock();
        return st;
    }

    {
        const uint8_t *active = (const uint8_t *)nvs_page_base(s_active_page);

        (void)memcpy(s_boot_rsvd_copy, active + NVS_BOOT_RSVD_OFFSET, NVS_BOOT_RSVD_SIZE);
    }

    cfg = (boot_slot_cfg_t *)s_boot_rsvd_copy;
    cfg->magic = BOOT_SLOT_MAGIC;
    cfg->slot = slot;

    s_boot_rsvd_override = 1;
    st = nvs_commit_page_payload(s_nvs_payload, payload_len);
    nvs_unlock();
    return st;
}
