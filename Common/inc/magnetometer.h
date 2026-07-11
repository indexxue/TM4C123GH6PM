/**
 * @file    magnetometer.h
 * @brief   QMC5883P 三轴磁力计公共模块（I2C0 @ BOARD_I2C_CFG）
 */

#ifndef MAGNETOMETER_H
#define MAGNETOMETER_H

#include "type.h"

#include <stdint.h>

/** 厂测扫描地址：0x2C（PB2/PB3 I2C0） */
#define MAGNETOMETER_I2C_ADDR_DEFAULT 0x2Cu

typedef struct {
    int16_t mx;
    int16_t my;
    int16_t mz;
} magnetometer_sample_t;

status_t magnetometer_init(void);
bool_t magnetometer_is_ready(void);
status_t magnetometer_read_sample(magnetometer_sample_t *sample);
/** 连续模式非阻塞读，供姿态周期任务使用（勿在厂测首读路径使用） */
status_t magnetometer_read_sample_fast(magnetometer_sample_t *sample);

#endif /* MAGNETOMETER_H */
