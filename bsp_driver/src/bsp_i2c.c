/**
 * @file bsp_i2c.c
 * @brief TM4C123 I2C 主机
 */

#include "bsp_i2c.h"

#include "bsp_bus_lock.h"
#include "bsp_systick.h"
#include "bsp_sysctl.h"

#include "bsp_config.h"

#include "driverlib/i2c.h"
#include "driverlib/sysctl.h"
#include "inc/hw_memmap.h"

#define BSP_I2C_PERIPH_READY_US 100000U
#define BSP_I2C_OP_TIMEOUT_US   10000U

static uint32_t i2c_periph_from_base(uint32_t base)
{
    switch (base) {
    case I2C0_BASE:
        return SYSCTL_PERIPH_I2C0;
    case I2C1_BASE:
        return SYSCTL_PERIPH_I2C1;
    case I2C2_BASE:
        return SYSCTL_PERIPH_I2C2;
    case I2C3_BASE:
        return SYSCTL_PERIPH_I2C3;
    case I2C4_BASE:
        return SYSCTL_PERIPH_I2C4;
    case I2C5_BASE:
        return SYSCTL_PERIPH_I2C5;
    case I2C6_BASE:
        return SYSCTL_PERIPH_I2C6;
    case I2C7_BASE:
        return SYSCTL_PERIPH_I2C7;
    case I2C8_BASE:
        return SYSCTL_PERIPH_I2C8;
    case I2C9_BASE:
        return SYSCTL_PERIPH_I2C9;
    default:
        return 0U;
    }
}

static void i2c_bus_recover(uint32_t base)
{
    I2CMasterControl(base, I2C_MASTER_CMD_BURST_SEND_ERROR_STOP);
    (void)I2CMasterErr(base);
}

static bool i2c_wait_idle(uint32_t base, uint32_t timeout_us)
{
    bsp_timeout_t timeout;

    bsp_timeout_start_us(&timeout, timeout_us);
    while (I2CMasterBusy(base)) {
        if (bsp_timeout_expired(&timeout)) {
            i2c_bus_recover(base);
            return false;
        }
    }

    if (I2CMasterErr(base) != I2C_MASTER_ERR_NONE) {
        i2c_bus_recover(base);
        return false;
    }

    return true;
}

static bool i2c_put_byte(uint32_t base, uint8_t value, bool finish)
{
    I2CMasterDataPut(base, value);
    I2CMasterControl(base, finish ? I2C_MASTER_CMD_BURST_SEND_FINISH : I2C_MASTER_CMD_BURST_SEND_CONT);
    return i2c_wait_idle(base, BSP_I2C_OP_TIMEOUT_US);
}

static bool i2c_write_locked(uint32_t base, uint8_t addr_7bit, uint8_t reg, const uint8_t *data, size_t len)
{
    size_t i;

    I2CMasterSlaveAddrSet(base, addr_7bit, false);
    I2CMasterControl(base, I2C_MASTER_CMD_BURST_SEND_START);
    if (!i2c_wait_idle(base, BSP_I2C_OP_TIMEOUT_US)) {
        return false;
    }

    if (!i2c_put_byte(base, reg, len == 0U)) {
        return false;
    }

    for (i = 0U; i < len; i++) {
        if (!i2c_put_byte(base, data[i], i == (len - 1U))) {
            return false;
        }
    }

    return true;
}

static bool i2c_read_locked(uint32_t base, uint8_t addr_7bit, uint8_t reg, uint8_t *data, size_t len)
{
    size_t i;

    if ((data == NULL) || (len == 0U)) {
        return false;
    }

    I2CMasterSlaveAddrSet(base, addr_7bit, false);
    I2CMasterControl(base, I2C_MASTER_CMD_BURST_SEND_START);
    if (!i2c_wait_idle(base, BSP_I2C_OP_TIMEOUT_US)) {
        return false;
    }

    I2CMasterDataPut(base, reg);
    I2CMasterControl(base, I2C_MASTER_CMD_BURST_SEND_FINISH);
    if (!i2c_wait_idle(base, BSP_I2C_OP_TIMEOUT_US)) {
        return false;
    }

    I2CMasterSlaveAddrSet(base, addr_7bit, true);
    if (len == 1U) {
        I2CMasterControl(base, I2C_MASTER_CMD_SINGLE_RECEIVE);
        if (!i2c_wait_idle(base, BSP_I2C_OP_TIMEOUT_US)) {
            return false;
        }
        data[0] = (uint8_t)I2CMasterDataGet(base);
        I2CMasterControl(base, I2C_MASTER_CMD_SINGLE_SEND);
        return i2c_wait_idle(base, BSP_I2C_OP_TIMEOUT_US);
    }

    I2CMasterControl(base, I2C_MASTER_CMD_BURST_RECEIVE_START);
    if (!i2c_wait_idle(base, BSP_I2C_OP_TIMEOUT_US)) {
        return false;
    }

    for (i = 0U; i < len; i++) {
        if (i == (len - 1U)) {
            I2CMasterControl(base, I2C_MASTER_CMD_BURST_RECEIVE_FINISH);
        } else {
            I2CMasterControl(base, I2C_MASTER_CMD_BURST_RECEIVE_CONT);
        }
        if (!i2c_wait_idle(base, BSP_I2C_OP_TIMEOUT_US)) {
            return false;
        }
        data[i] = (uint8_t)I2CMasterDataGet(base);
    }

    return true;
}

bool bsp_i2c_init(const bsp_i2c_config_t *cfg)
{
    uint32_t periph;

    if ((cfg == NULL) || (cfg->base == 0U) || (cfg->clock_hz == 0U)) {
        return false;
    }

    periph = i2c_periph_from_base(cfg->base);
    if (periph == 0U) {
        return false;
    }

    SysCtlPeripheralEnable(periph);
    if (!bsp_periph_wait_ready(periph, BSP_I2C_PERIPH_READY_US)) {
        return false;
    }

    I2CMasterInitExpClk(cfg->base, bsp_clock_get_hz(), cfg->clock_hz > 100000U);
    I2CMasterEnable(cfg->base);
    return true;
}

bool bsp_i2c_probe(uint32_t base, uint8_t addr_7bit)
{
    bool ok;

    if (!bsp_bus_lock_i2c(base, 0U)) {
        return false;
    }

    I2CMasterSlaveAddrSet(base, addr_7bit, false);
    I2CMasterControl(base, I2C_MASTER_CMD_SINGLE_SEND);
    ok = i2c_wait_idle(base, BSP_I2C_OP_TIMEOUT_US);
    bsp_bus_unlock_i2c(base);
    return ok;
}

bool bsp_i2c_write(uint32_t base, uint8_t addr_7bit, uint8_t reg, const uint8_t *data, size_t len)
{
    bool ok;

    if ((data == NULL) && (len > 0U)) {
        return false;
    }

    if (!bsp_bus_lock_i2c(base, 0U)) {
        return false;
    }

    ok = i2c_write_locked(base, addr_7bit, reg, data, len);
    bsp_bus_unlock_i2c(base);
    return ok;
}

bool bsp_i2c_read(uint32_t base, uint8_t addr_7bit, uint8_t reg, uint8_t *data, size_t len)
{
    bool ok;

    if (!bsp_bus_lock_i2c(base, 0U)) {
        return false;
    }

    ok = i2c_read_locked(base, addr_7bit, reg, data, len);
    bsp_bus_unlock_i2c(base);
    return ok;
}

bool bsp_i2c_write_byte(uint32_t base, uint8_t addr_7bit, uint8_t reg, uint8_t value)
{
    return bsp_i2c_write(base, addr_7bit, reg, &value, 1U);
}

bool bsp_i2c_read_byte(uint32_t base, uint8_t addr_7bit, uint8_t reg, uint8_t *value)
{
    return bsp_i2c_read(base, addr_7bit, reg, value, 1U);
}
