/**
 * @file bsp_spi.h
 * @brief TM4C123 SPI 薄封装（硬件为 SSI）
 */

#ifndef BSP_DRIVER_SPI_H
#define BSP_DRIVER_SPI_H

#include "bsp_gpio.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    BSP_SPI_MODE_0 = 0,
    BSP_SPI_MODE_1,
    BSP_SPI_MODE_2,
    BSP_SPI_MODE_3,
} bsp_spi_mode_t;

typedef struct {
    uint32_t base;
    uint32_t clock_hz;
    bsp_spi_mode_t mode;
    uint8_t data_bits;
} bsp_spi_config_t;

typedef struct {
    const bsp_gpio_pin_t *cs_pin;
    bool cs_active_low;
} bsp_spi_cs_t;

bool bsp_spi_init(const bsp_spi_config_t *cfg);
void bsp_spi_cs_set(const bsp_spi_cs_t *cs, bool selected);
bool bsp_spi_transfer(uint32_t base, uint8_t tx, uint8_t *rx);
bool bsp_spi_write(uint32_t base, const uint8_t *data, size_t len);
bool bsp_spi_read(uint32_t base, uint8_t *data, size_t len);
bool bsp_spi_transceive(uint32_t base, const bsp_spi_cs_t *cs,
                        const uint8_t *tx, uint8_t *rx, size_t len);

#endif /* BSP_DRIVER_SPI_H */
