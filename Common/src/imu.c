/**
 * @file    imu.c
 * @brief   MPU6050 六轴 IMU 公共模块（I2C0 @ 0x69）
 */

#include "imu.h"

#include "board.h"
#include "bsp_i2c.h"
#include "bsp_systick.h"
#include "log.h"
#include "mpu6050.h"

#define IMU_INIT_RETRY_MAX      5U
#define IMU_INIT_RETRY_MS       25U
#define IMU_PWRUP_DELAY_MS      20U

static mpu6050_t s_mpu;
static bool_t s_ready;

static void imu_i2c_bus_prepare(void)
{
    (void)bsp_i2c_init(&BOARD_I2C_CFG);
}

static void imu_i2c_bus_recover(void)
{
    bsp_i2c_bus_release_idle_high(BOARD_I2C_CFG.base);
    (void)bsp_i2c_init(&BOARD_I2C_CFG);
}

static void imu_log_probe(mpu6050_status_t last_st)
{
    bool scl_high = false;
    bool sda_high = false;
    uint8_t whoami = 0U;

    (void)bsp_i2c0_sample_idle_lines(&scl_high, &sda_high);
    LOG_WARN("imu: init fail st=%d scl=%u sda=%u",
             (int)last_st,
             (unsigned)scl_high,
             (unsigned)sda_high);

    if (bsp_i2c_read_byte(BOARD_I2C_CFG.base, IMU_I2C_ADDR_DEFAULT, 0x75u, &whoami)) {
        LOG_WARN("imu: whoami=0x%02X expect 0x68 @0x%02X",
                 (unsigned)whoami,
                 (unsigned)IMU_I2C_ADDR_DEFAULT);
    } else {
        LOG_WARN("imu: whoami read fail @0x%02X", (unsigned)IMU_I2C_ADDR_DEFAULT);
    }
}

static int imu_i2c_write(uint8_t addr, const uint8_t *data, uint16_t len)
{
    if ((data == NULL) || (len < 2u)) {
        return -1;
    }

    return bsp_i2c_write(BOARD_I2C_CFG.base, addr, data[0], &data[1], (size_t)(len - 1u)) ? 0 : -1;
}

static int imu_i2c_read(uint8_t addr, uint8_t reg, uint8_t *data, uint16_t len)
{
    return bsp_i2c_read(BOARD_I2C_CFG.base, addr, reg, data, (size_t)len) ? 0 : -1;
}

static void imu_delay_ms(uint32_t ms)
{
    bsp_delay_ms(ms);
}

status_t imu_init(void)
{
    mpu6050_config_t cfg = {0};
    mpu6050_status_t st = MPU6050_ERROR_I2C;
    uint32_t attempt;

    if (s_ready != FALSE) {
        return STATUS_OK;
    }

    imu_i2c_bus_prepare();
    bsp_delay_ms(IMU_PWRUP_DELAY_MS);

    cfg.write         = imu_i2c_write;
    cfg.read          = imu_i2c_read;
    cfg.delay_ms      = imu_delay_ms;
    cfg.address       = IMU_I2C_ADDR_DEFAULT;
    cfg.skip_id_check = false;

    for (attempt = 0U; attempt < IMU_INIT_RETRY_MAX; attempt++) {
        st = mpu6050_init_with_config(&s_mpu, &cfg);
        if (st == MPU6050_OK) {
            s_ready = TRUE;
            return STATUS_OK;
        }

        if ((attempt + 1U) < IMU_INIT_RETRY_MAX) {
            LOG_WARN("imu: init retry %u/%u st=%d",
                     (unsigned)(attempt + 1U),
                     (unsigned)IMU_INIT_RETRY_MAX,
                     (int)st);
            imu_i2c_bus_recover();
            bsp_delay_ms(IMU_INIT_RETRY_MS);
        }
    }

    imu_log_probe(st);
    return STATUS_FAIL;
}

bool_t imu_is_ready(void)
{
    return s_ready;
}

status_t imu_read_sample(imu_sample_t *sample)
{
    if (s_ready == FALSE) {
        return STATUS_INVALID_STATE;
    }
    if (sample == NULL) {
        return STATUS_INVALID_ARG;
    }

    return (mpu6050_read_raw(&s_mpu,
                             &sample->ax,
                             &sample->ay,
                             &sample->az,
                             &sample->gx,
                             &sample->gy,
                             &sample->gz) == MPU6050_OK)
               ? STATUS_OK
               : STATUS_FAIL;
}

status_t imu_read_temperature(int16_t *temp_c)
{
    float temp_f = 0.0f;

    if (s_ready == FALSE) {
        return STATUS_INVALID_STATE;
    }
    if (temp_c == NULL) {
        return STATUS_INVALID_ARG;
    }

    if (mpu6050_read_temperature(&s_mpu, &temp_f) != MPU6050_OK) {
        return STATUS_FAIL;
    }

    if (temp_f >= 0.0f) {
        *temp_c = (int16_t)(temp_f + 0.5f);
    } else {
        *temp_c = (int16_t)(temp_f - 0.5f);
    }

    return STATUS_OK;
}
