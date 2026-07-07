/**
 * @file    bootloader.h
 * @brief   TM4C123 Bootloader：双分区校验与跳转
 */

#ifndef BOOTLOADER_H
#define BOOTLOADER_H

#include <stdbool.h>
#include <stdint.h>

bool boot_app_is_valid(uint32_t app_base);
void boot_app_jump(uint32_t app_base);

#endif /* BOOTLOADER_H */
