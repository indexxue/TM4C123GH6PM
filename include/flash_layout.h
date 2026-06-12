/**
 * @file    flash_layout.h
 * @brief   TM4C123 片上 Flash 分区常量（Boot + APP_A + APP_B + NVS）
 *
 * 详见 PARTITION.md / docs/flash-partition.md
 */

#ifndef FLASH_LAYOUT_H
#define FLASH_LAYOUT_H

#include <stdint.h>

#define FLASH_BL_BASE           0x00000000U
#define FLASH_BL_SIZE           0x00004000U   /* 16 KB */
#define FLASH_BL_END            (FLASH_BL_BASE + FLASH_BL_SIZE - 1U)

#define FLASH_APP_A_BASE        0x00004000U
#define FLASH_APP_A_SIZE        0x0001D000U   /* 116 KB — 唯一运行槽 */
#define FLASH_APP_A_END         0x00020FFFU

#define FLASH_APP_B_BASE        0x00021000U
#define FLASH_APP_B_SIZE        0x0001D000U   /* 116 KB — OTA/厂测存储 */
#define FLASH_APP_B_END         0x0003DFFFU

#define FLASH_STAGING_BASE      FLASH_APP_B_BASE
#define FLASH_STAGING_SIZE      FLASH_APP_B_SIZE
#define FLASH_OTA_IMAGE_MAX     FLASH_APP_A_SIZE

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

#endif /* FLASH_LAYOUT_H */
