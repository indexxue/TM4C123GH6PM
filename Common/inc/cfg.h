/**
 * @file    cfg.h
 * @brief   业务层读取 NVS 配置（电机极性、编码器零点、循迹阈值、电池标定）
 */

#ifndef CFG_H
#define CFG_H

#include "nvs.h"

#include <stdint.h>

void cfg_init(void);

int32_t cfg_motor_rpm(uint8_t motor_id, int32_t rpm);
int32_t cfg_encoder_count(uint8_t index);
/** 按 encoder_dir_mask 翻转编码器 delta（与 motor_dir 解耦） */
int32_t cfg_encoder_delta(uint8_t wheel_index, int32_t delta);
uint16_t cfg_line_threshold(uint8_t sensor_index);
uint32_t cfg_battery_calibrate_mv(uint32_t raw_mv);

const nvs_spd_limit_t *cfg_spd_limit(void);
const nvs_kinematics_t *cfg_kinematics(void);
const nvs_pid3_t *cfg_pid_speed(void);
const nvs_pid3_t *cfg_pid_line(void);

#endif /* CFG_H */
