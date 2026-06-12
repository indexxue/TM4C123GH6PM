/**
 * @file    bootloader.h
 * @brief   TM4C123 Bootloader 公共接口
 */

#ifndef BOOTLOADER_H
#define BOOTLOADER_H

#include <stdbool.h>
#include <stdint.h>

#include "ota_meta.h"

bool boot_app_is_valid(uint32_t app_base);
void boot_app_jump(uint32_t app_base);

int boot_ota_meta_read(ota_meta_t *out);

#endif /* BOOTLOADER_H */
