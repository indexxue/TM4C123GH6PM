/**
 * @file bsp_qei.h
 * @brief TM4C123 QEI 编码器薄封装
 */

#ifndef BSP_DRIVER_QEI_H
#define BSP_DRIVER_QEI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t qei_base;
    uint32_t qei_periph;
} bsp_qei_channel_t;

typedef struct {
    const bsp_qei_channel_t *channels;
    size_t channel_count;
} bsp_qei_config_t;

bool bsp_qei_init(const bsp_qei_config_t *cfg);
int32_t bsp_qei_get_position(uint32_t qei_base);
void bsp_qei_reset(uint32_t qei_base);

#endif /* BSP_DRIVER_QEI_H */
