/**
 * @file bsp_dma.h
 * @brief TM4C123 uDMA 薄封装
 */

#ifndef BSP_DRIVER_DMA_H
#define BSP_DRIVER_DMA_H

#include <stdbool.h>
#include <stdint.h>

bool bsp_dma_init(void);
bool bsp_dma_is_ready(void);

#endif /* BSP_DRIVER_DMA_H */
