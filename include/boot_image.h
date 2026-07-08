/**
 * @file    boot_image.h
 * @brief   应用镜像向量表合法性检查（Boot / 分区切换共用）
 */

#ifndef BOOT_IMAGE_H
#define BOOT_IMAGE_H

#include <stdbool.h>
#include <stdint.h>

#include "flash_layout.h"

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

#endif /* BOOT_IMAGE_H */
