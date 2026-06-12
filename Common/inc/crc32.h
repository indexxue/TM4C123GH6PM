/**
 * @file    crc32.h
 * @brief   CRC32（与 zlib.crc32 / Python 主机工具一致）
 */

#ifndef CRC32_H
#define CRC32_H

#include <stdint.h>
#include <stddef.h>

#define CRC32_INIT_VALUE 0xFFFFFFFFU

uint32_t crc32_update(uint32_t crc, const void *data, size_t len);
uint32_t crc32_finalize(uint32_t crc);
uint32_t crc32_compute(const void *data, size_t len);

#endif /* CRC32_H */
