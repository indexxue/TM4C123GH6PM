/**
 * @file    magnetometer.c
 * @brief   QMC5883P 三轴磁力计公共模块（I2C0）
 */

#include "magnetometer.h"

#include "board.h"
#include "bsp_i2c.h"
#include "bsp_systick.h"
#include "qmc5883p.h"

static qmc5883p_t s_dev;
static bool_t s_ready;

static void magnetometer_i2c_bus_prepare(void)
{
    (void)bsp_i2c_init(&BOARD_I2C_CFG);
}

static int magnetometer_i2c_write(uint8_t addr, const uint8_t *data, uint16_t len)
{
    uint32_t base = BOARD_I2C_CFG.base;

    if ((data == NULL) || (len == 0u)) {
        return -1;
    }

    if (len == 1u) {
        return bsp_i2c_write_byte(base, addr, data[0], data[0]) ? 0 : -1;
    }

    return bsp_i2c_write(base, addr, data[0], &data[1], (size_t)(len - 1u)) ? 0 : -1;
}

static int magnetometer_i2c_read(uint8_t addr, uint8_t reg, uint8_t *data, uint16_t len)
{
    return bsp_i2c_read(BOARD_I2C_CFG.base, addr, reg, data, (size_t)len) ? 0 : -1;
}

static void magnetometer_delay_ms(uint32_t ms)
{
    bsp_delay_ms(ms);
}

static status_t magnetometer_status_from_qmc(qmc5883p_status_t st)
{
    switch (st) {
    case QMC5883P_OK:
        return STATUS_OK;
    case QMC5883P_ERROR_PARAM:
        return STATUS_INVALID_ARG;
    case QMC5883P_ERROR_NOT_INIT:
        return STATUS_INVALID_STATE;
    default:
        return STATUS_FAIL;
    }
}

status_t magnetometer_init(void)
{
    qmc5883p_config_t cfg = {0};
    qmc5883p_status_t st;

    if (s_ready != FALSE) {
        return STATUS_OK;
    }

    magnetometer_i2c_bus_prepare();

    cfg.write         = magnetometer_i2c_write;
    cfg.read          = magnetometer_i2c_read;
    cfg.delay_ms      = magnetometer_delay_ms;
    cfg.address       = MAGNETOMETER_I2C_ADDR_DEFAULT;
    cfg.skip_id_check = false;

    st = qmc5883p_init_with_config(&s_dev, &cfg);
    if (st != QMC5883P_OK) {
        return magnetometer_status_from_qmc(st);
    }

    s_ready = TRUE;
    return STATUS_OK;
}

bool_t magnetometer_is_ready(void)
{
    return s_ready;
}

status_t magnetometer_read_sample(magnetometer_sample_t *sample)
{
    qmc5883p_status_t st;

    if (s_ready == FALSE) {
        return STATUS_INVALID_STATE;
    }
    if (sample == NULL) {
        return STATUS_INVALID_ARG;
    }

    st = qmc5883p_read_raw(&s_dev, &sample->mx, &sample->my, &sample->mz);
    return magnetometer_status_from_qmc(st);
}

status_t magnetometer_read_sample_fast(magnetometer_sample_t *sample)
{
    qmc5883p_status_t st;

    if (s_ready == FALSE) {
        return STATUS_INVALID_STATE;
    }
    if (sample == NULL) {
        return STATUS_INVALID_ARG;
    }

    st = qmc5883p_read_raw_nowait(&s_dev, &sample->mx, &sample->my, &sample->mz);
    return magnetometer_status_from_qmc(st);
}
