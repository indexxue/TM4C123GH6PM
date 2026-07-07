/**
 * @file bsp_i2c.h
 * @brief TM4C123 I2C 主机薄封装
 */

#ifndef BSP_DRIVER_I2C_H
#define BSP_DRIVER_I2C_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t base;
    uint32_t clock_hz;
} bsp_i2c_config_t;

bool bsp_i2c_init(const bsp_i2c_config_t *cfg);
bool bsp_i2c_probe(uint32_t base, uint8_t addr_7bit);
bool bsp_i2c_write(uint32_t base, uint8_t addr_7bit, uint8_t reg, const uint8_t *data, size_t len);
bool bsp_i2c_read(uint32_t base, uint8_t addr_7bit, uint8_t reg, uint8_t *data, size_t len);
bool bsp_i2c_write_byte(uint32_t base, uint8_t addr_7bit, uint8_t reg, uint8_t value);
bool bsp_i2c_read_byte(uint32_t base, uint8_t addr_7bit, uint8_t reg, uint8_t *value);

#endif /* BSP_DRIVER_I2C_H */
