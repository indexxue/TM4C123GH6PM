/**
 * @file    crc32.c
 * @brief   CRC32 IEEE 802.3（zlib 兼容）
 */

#include "crc32.h"

static uint32_t crc32_table[256U];
static int crc32_table_ready;

static void crc32_init_table(void)
{
    uint32_t i;
    uint32_t j;

    for (i = 0U; i < 256U; i++) {
        uint32_t c = i;
        for (j = 0U; j < 8U; j++) {
            if ((c & 1U) != 0U) {
                c = 0xEDB88320U ^ (c >> 1U);
            } else {
                c >>= 1U;
            }
        }
        crc32_table[i] = c;
    }
    crc32_table_ready = 1;
}

uint32_t crc32_update(uint32_t crc, const void *data, size_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    size_t i;

    if (crc32_table_ready == 0) {
        crc32_init_table();
    }

    for (i = 0U; i < len; i++) {
        crc = crc32_table[(crc ^ p[i]) & 0xFFU] ^ (crc >> 8U);
    }
    return crc;
}

uint32_t crc32_finalize(uint32_t crc)
{
    return crc ^ CRC32_INIT_VALUE;
}

uint32_t crc32_compute(const void *data, size_t len)
{
    return crc32_finalize(crc32_update(CRC32_INIT_VALUE, data, len));
}
