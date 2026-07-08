/**
 * @file    bootloader.h
 * @brief   TM4C123 Bootloader 跳转 API
 */

#ifndef BOOTLOADER_H
#define BOOTLOADER_H

#include <stdint.h>

void boot_app_jump(uint32_t app_base);

#endif /* BOOTLOADER_H */
