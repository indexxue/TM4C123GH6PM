/**
 * @file bsp_spi.c
 * @brief TM4C123 SSI（SPI）主机
 */

#include "bsp_spi.h"

#include "bsp_bus_lock.h"
#include "bsp_systick.h"
#include "bsp_sysctl.h"

#include "bsp_config.h"

#include "driverlib/ssi.h"
#include "driverlib/sysctl.h"
#include "inc/hw_memmap.h"

#define BSP_SPI_PERIPH_READY_US 100000U
#define BSP_SPI_OP_TIMEOUT_US   10000U

static uint32_t spi_periph_from_base(uint32_t base)
{
    switch (base) {
    case SSI0_BASE:
        return SYSCTL_PERIPH_SSI0;
    case SSI1_BASE:
        return SYSCTL_PERIPH_SSI1;
    case SSI2_BASE:
        return SYSCTL_PERIPH_SSI2;
    case SSI3_BASE:
        return SYSCTL_PERIPH_SSI3;
    default:
        return 0U;
    }
}

static uint32_t spi_frame_mode(bsp_spi_mode_t mode)
{
    switch (mode) {
    case BSP_SPI_MODE_1:
        return SSI_FRF_MOTO_MODE_1;
    case BSP_SPI_MODE_2:
        return SSI_FRF_MOTO_MODE_2;
    case BSP_SPI_MODE_3:
        return SSI_FRF_MOTO_MODE_3;
    case BSP_SPI_MODE_0:
    default:
        return SSI_FRF_MOTO_MODE_0;
    }
}

static void spi_drain_rx(uint32_t base)
{
    uint32_t junk;

    while (SSIDataGetNonBlocking(base, &junk)) {
    }
}

static bool spi_wait_idle(uint32_t base, uint32_t timeout_us)
{
    bsp_timeout_t timeout;

    bsp_timeout_start_us(&timeout, timeout_us);
    while (SSIBusy(base)) {
        if (bsp_timeout_expired(&timeout)) {
            return false;
        }
    }

    return true;
}

static bool spi_transfer_locked(uint32_t base, uint8_t tx, uint8_t *rx)
{
    if (spi_periph_from_base(base) == 0U) {
        return false;
    }

    if (!spi_wait_idle(base, BSP_SPI_OP_TIMEOUT_US)) {
        return false;
    }

    SSIDataPut(base, tx);
    if (!spi_wait_idle(base, BSP_SPI_OP_TIMEOUT_US)) {
        return false;
    }

    if (rx != NULL) {
        uint32_t raw;

        SSIDataGet(base, &raw);
        *rx = (uint8_t)raw;
    } else {
        uint32_t junk;

        SSIDataGet(base, &junk);
    }

    return true;
}

bool bsp_spi_init(const bsp_spi_config_t *cfg)
{
    uint32_t periph;
    uint8_t data_bits;

    if ((cfg == NULL) || (cfg->base == 0U) || (cfg->clock_hz == 0U)) {
        return false;
    }

    data_bits = cfg->data_bits;
    if ((data_bits < 4U) || (data_bits > 16U)) {
        data_bits = 8U;
    }

    periph = spi_periph_from_base(cfg->base);
    if (periph == 0U) {
        return false;
    }

    SysCtlPeripheralEnable(periph);
    if (!bsp_periph_wait_ready(periph, BSP_SPI_PERIPH_READY_US)) {
        return false;
    }

    SSIConfigSetExpClk(cfg->base, bsp_clock_get_hz(), spi_frame_mode(cfg->mode),
                       SSI_MODE_MASTER, cfg->clock_hz, data_bits);
    SSIEnable(cfg->base);
    spi_drain_rx(cfg->base);
    return true;
}

void bsp_spi_cs_set(const bsp_spi_cs_t *cs, bool selected)
{
    bool level;

    if ((cs == NULL) || (cs->cs_pin == NULL)) {
        return;
    }

    level = cs->cs_active_low ? !selected : selected;
    bsp_gpio_write(cs->cs_pin, level);
}

bool bsp_spi_transfer(uint32_t base, uint8_t tx, uint8_t *rx)
{
    bool ok;

    if (!bsp_bus_lock_spi(base, 0U)) {
        return false;
    }

    ok = spi_transfer_locked(base, tx, rx);
    bsp_bus_unlock_spi(base);
    return ok;
}

bool bsp_spi_write(uint32_t base, const uint8_t *data, size_t len)
{
    return bsp_spi_transceive(base, NULL, data, NULL, len);
}

bool bsp_spi_read(uint32_t base, uint8_t *data, size_t len)
{
    return bsp_spi_transceive(base, NULL, NULL, data, len);
}

bool bsp_spi_transceive(uint32_t base, const bsp_spi_cs_t *cs,
                        const uint8_t *tx, uint8_t *rx, size_t len)
{
    size_t i;
    bool ok = true;

    if (((tx == NULL) && (rx == NULL)) || (len == 0U)) {
        return false;
    }

    if (!bsp_bus_lock_spi(base, 0U)) {
        return false;
    }

    bsp_spi_cs_set(cs, true);
    for (i = 0U; i < len; i++) {
        uint8_t out = (tx != NULL) ? tx[i] : 0xFFU;
        uint8_t in;

        if (!spi_transfer_locked(base, out, &in)) {
            ok = false;
            break;
        }

        if (rx != NULL) {
            rx[i] = in;
        }
    }
    bsp_spi_cs_set(cs, false);
    bsp_bus_unlock_spi(base);
    return ok;
}
