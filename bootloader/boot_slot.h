/**
 * @file    boot_slot.h
 * @brief   Bootloader 运行槽选择（NVS 持久化，无 OTA 状态机）
 *
 * APP 或厂测固件写入 boot_slot_cfg 后复位，Bootloader 跳转 APP_A 或 APP_B。
 * 配置位于 NVS 页 0 数据区起始（偏移 16 B，紧接页头之后）。
 */

#ifndef BOOTLOADER_BOOT_SLOT_H
#define BOOTLOADER_BOOT_SLOT_H

#include <stdint.h>

#include "flash_layout.h"

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

#endif /* BOOTLOADER_BOOT_SLOT_H */
