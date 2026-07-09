/**
 * @file bsp_i2c.h
 * @brief TM4C123 I2C 薄封装（I2C0=PB2/PB3 GPIO 软件 I2C，其余实例走硬件主机）
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
/** I2C0 GPIO 位操作扫描（fallback） */
void bsp_i2c0_gpio_scan_begin(void);
bool bsp_i2c0_gpio_probe(uint8_t addr_7bit);
void bsp_i2c0_gpio_scan_end_idle_high(void);
/** I2C0 硬件主机扫描：begin → hw_probe 循环 → end */
void bsp_i2c0_hw_scan_begin(const bsp_i2c_config_t *cfg);
bool bsp_i2c0_hw_probe(uint8_t addr_7bit);
void bsp_i2c0_hw_scan_end(const bsp_i2c_config_t *cfg);
/** 扫描/异常后释放总线，避免 SCL 卡低；cfg 非 NULL 时必要时复位 I2C 主机 */
void bsp_i2c_bus_release(uint32_t base, const bsp_i2c_config_t *cfg);
/** 释放后保持 SCL/SDA 高（厂测用，不再 enable I2C 主机） */
void bsp_i2c_bus_release_idle_high(uint32_t base);
/** 采样 I2C0 PB2/PB3 空闲电平（1=高），不改变当前引脚配置 */
bool bsp_i2c0_sample_idle_lines(bool *scl_high, bool *sda_high);
bool bsp_i2c_write(uint32_t base, uint8_t addr_7bit, uint8_t reg, const uint8_t *data, size_t len);
bool bsp_i2c_read(uint32_t base, uint8_t addr_7bit, uint8_t reg, uint8_t *data, size_t len);
bool bsp_i2c_write_byte(uint32_t base, uint8_t addr_7bit, uint8_t reg, uint8_t value);
bool bsp_i2c_read_byte(uint32_t base, uint8_t addr_7bit, uint8_t reg, uint8_t *value);

#endif /* BSP_DRIVER_I2C_H */
