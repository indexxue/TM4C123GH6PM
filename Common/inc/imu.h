/**
 * @file    imu.h
 * @brief   MPU6050 六轴 IMU 公共模块（I2C0 @ 0x69）
 */

#ifndef IMU_H
#define IMU_H

#include "type.h"

#include <stdint.h>

/** 厂测扫描地址：0x69（PB2/PB3 I2C0，常见 MPU6050 AD0=1） */
#define IMU_I2C_ADDR_DEFAULT 0x69u

typedef struct {
    int16_t ax;
    int16_t ay;
    int16_t az;
    int16_t gx;
    int16_t gy;
    int16_t gz;
} imu_sample_t;

status_t imu_init(void);
bool_t imu_is_ready(void);
status_t imu_read_sample(imu_sample_t *sample);
status_t imu_read_temperature(int16_t *temp_c);

#endif /* IMU_H */
