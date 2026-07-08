/**
 * @file    flash_layout.h
 * @brief   TM4C123 片上 Flash 分区、Boot 运行槽与镜像合法性检查
 *
 * 详见 PARTITION.md / docs/flash-partition.md
 */

#ifndef FLASH_LAYOUT_H
#define FLASH_LAYOUT_H

#include <stdbool.h>
#include <stdint.h>

/* --- Flash 分区 --- */

#define FLASH_BL_BASE           0x00000000U
#define FLASH_BL_SIZE           0x00004000U   /* 16 KB */
#define FLASH_BL_END            (FLASH_BL_BASE + FLASH_BL_SIZE - 1U)

#define FLASH_APP_A_BASE        0x00004000U
#define FLASH_APP_A_SIZE        0x0001D000U   /* 116 KB — 唯一运行槽 */
#define FLASH_APP_A_END         0x00020FFFU

#define FLASH_APP_B_BASE        0x00021000U
#define FLASH_APP_B_SIZE        0x0001D000U   /* 116 KB — 厂测镜像存储 */
#define FLASH_APP_B_END         0x0003DFFFU

#define FLASH_NVS_BASE          0x0003E000U
#define FLASH_NVS_SIZE          0x00002000U   /* 8 KB */
#define FLASH_NVS_END           0x0003FFFFU

#define FLASH_CHIP_SIZE         0x00040000U   /* 256 KB */

#define FLASH_ERASE_BLOCK_SIZE  1024U
#define FLASH_PROGRAM_ALIGN     4U

#define FLASH_RUN_SLOT          FLASH_APP_A_BASE

static inline int flash_addr_in_range(uint32_t addr, uint32_t base, uint32_t size)
{
    return (addr >= base) && (addr < (base + size));
}

/* --- Boot 运行槽（NVS 页头后 256 B，见 nvs.c） --- */

#define BOOT_NVS_PAGE_MAGIC     0x4E565331U   /* "NVS1" */
#define BOOT_NVS_PAGE_VERSION   1U
#define BOOT_NVS_PAGE_HDR_SIZE  16U
#define BOOT_SLOT_CFG_OFFSET    BOOT_NVS_PAGE_HDR_SIZE

#define BOOT_SLOT_MAGIC         0x534C4F54U   /* "SLOT" */
#define BOOT_SLOT_A             0U
#define BOOT_SLOT_B             1U

typedef struct {
    uint32_t magic;
    uint32_t slot;
} boot_slot_cfg_t;

static inline bool boot_nvs_page0_valid(void)
{
    const uint32_t *hdr = (const uint32_t *)FLASH_NVS_BASE;

    return (hdr[0] == BOOT_NVS_PAGE_MAGIC) && (hdr[1] == BOOT_NVS_PAGE_VERSION);
}

static inline uint32_t boot_slot_read(void)
{
    const boot_slot_cfg_t *cfg =
        (const boot_slot_cfg_t *)(FLASH_NVS_BASE + BOOT_SLOT_CFG_OFFSET);

    if (!boot_nvs_page0_valid()) {
        return BOOT_SLOT_A;
    }
    if (cfg->magic != BOOT_SLOT_MAGIC) {
        return BOOT_SLOT_A;
    }
    if (cfg->slot > BOOT_SLOT_B) {
        return BOOT_SLOT_A;
    }
    return cfg->slot;
}

static inline uint32_t boot_slot_target_base(uint32_t slot)
{
    return (slot == BOOT_SLOT_B) ? FLASH_APP_B_BASE : FLASH_APP_A_BASE;
}

/* --- 应用镜像向量表合法性（Boot / 分区切换共用） --- */

static inline bool boot_image_range_ok(uint32_t app_base, uint32_t *app_end_out)
{
    if (app_base == FLASH_APP_A_BASE) {
        *app_end_out = FLASH_APP_A_END;
        return true;
    }
    if (app_base == FLASH_APP_B_BASE) {
        *app_end_out = FLASH_APP_B_END;
        return true;
    }
    return false;
}

static inline bool boot_image_is_valid(uint32_t app_base)
{
    const uint32_t *vt = (const uint32_t *)app_base;
    uint32_t sp;
    uint32_t reset;
    uint32_t reset_addr;
    uint32_t app_end;

    if (!boot_image_range_ok(app_base, &app_end)) {
        return false;
    }

    sp = vt[0];
    reset = vt[1];

    if ((sp == 0xFFFFFFFFU) || (reset == 0xFFFFFFFFU)) {
        return false;
    }

    if ((sp <= 0x20000000U) || (sp > 0x20008000U)) {
        return false;
    }

    if ((reset & 1U) == 0U) {
        return false;
    }

    reset_addr = reset & ~1U;
    if ((reset_addr < app_base) || (reset_addr > app_end)) {
        return false;
    }

    return true;
}

#endif /* FLASH_LAYOUT_H */
