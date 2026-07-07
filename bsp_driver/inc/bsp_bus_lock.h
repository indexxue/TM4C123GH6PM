/**
 * @file bsp_bus_lock.h
 * @brief I2C / SPI 总线互斥（FreeRTOS mutex）
 */

#ifndef BSP_DRIVER_BUS_LOCK_H
#define BSP_DRIVER_BUS_LOCK_H

#include <stdbool.h>
#include <stdint.h>

bool bsp_bus_lock_i2c(uint32_t base, uint32_t timeout_ms);
void bsp_bus_unlock_i2c(uint32_t base);
bool bsp_bus_lock_spi(uint32_t base, uint32_t timeout_ms);
void bsp_bus_unlock_spi(uint32_t base);

#endif /* BSP_DRIVER_BUS_LOCK_H */
