/**
 * @file bsp_bus_lock.c
 * @brief 按总线实例懒创建 FreeRTOS mutex
 */

#include "bsp_bus_lock.h"

#include "bsp_config.h"

#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"
#include "inc/hw_memmap.h"

#define BSP_BUS_LOCK_MS_DEFAULT 100U

static SemaphoreHandle_t s_i2c_mutex[10];
static SemaphoreHandle_t s_spi_mutex[4];

static SemaphoreHandle_t *i2c_mutex_slot(uint32_t base)
{
    switch (base) {
    case I2C0_BASE:
        return &s_i2c_mutex[0];
    case I2C1_BASE:
        return &s_i2c_mutex[1];
    case I2C2_BASE:
        return &s_i2c_mutex[2];
    case I2C3_BASE:
        return &s_i2c_mutex[3];
    case I2C4_BASE:
        return &s_i2c_mutex[4];
    case I2C5_BASE:
        return &s_i2c_mutex[5];
    case I2C6_BASE:
        return &s_i2c_mutex[6];
    case I2C7_BASE:
        return &s_i2c_mutex[7];
    case I2C8_BASE:
        return &s_i2c_mutex[8];
    case I2C9_BASE:
        return &s_i2c_mutex[9];
    default:
        return NULL;
    }
}

static SemaphoreHandle_t *spi_mutex_slot(uint32_t base)
{
    switch (base) {
    case SSI0_BASE:
        return &s_spi_mutex[0];
    case SSI1_BASE:
        return &s_spi_mutex[1];
    case SSI2_BASE:
        return &s_spi_mutex[2];
    case SSI3_BASE:
        return &s_spi_mutex[3];
    default:
        return NULL;
    }
}

static bool bus_lock(SemaphoreHandle_t *slot, uint32_t timeout_ms)
{
    if (slot == NULL) {
        return false;
    }

    if (xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED) {
        return true;
    }

    if (*slot == NULL) {
        *slot = xSemaphoreCreateMutex();
        if (*slot == NULL) {
            return false;
        }
    }

    if (timeout_ms == 0U) {
        timeout_ms = BSP_BUS_LOCK_MS_DEFAULT;
    }

    return xSemaphoreTake(*slot, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

static void bus_unlock(SemaphoreHandle_t *slot)
{
    if (xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED) {
        return;
    }

    if ((slot != NULL) && (*slot != NULL)) {
        (void)xSemaphoreGive(*slot);
    }
}

bool bsp_bus_lock_i2c(uint32_t base, uint32_t timeout_ms)
{
    return bus_lock(i2c_mutex_slot(base), timeout_ms);
}

void bsp_bus_unlock_i2c(uint32_t base)
{
    bus_unlock(i2c_mutex_slot(base));
}

bool bsp_bus_lock_spi(uint32_t base, uint32_t timeout_ms)
{
    return bus_lock(spi_mutex_slot(base), timeout_ms);
}

void bsp_bus_unlock_spi(uint32_t base)
{
    bus_unlock(spi_mutex_slot(base));
}
