/**
 * @file    cfg.c
 * @brief   业务层 NVS 配置访问
 */

#include "cfg.h"

#include "board.h"
#include "device_profile.h"
#include "log.h"

void cfg_init(void)
{
    const device_product_profile_t *profile = device_profile_product();
    const nvs_cfg_t *n = nvs_cfg_get();

    if (nvs_first_boot()) {
        LOG_INFO("cfg: %s id=%lu reboot=%lu sn=%s (init)",
                 profile->name,
                 (unsigned long)profile->product_id,
                 (unsigned long)n->boot_count,
                 n->serial);
        return;
    }

    LOG_INFO("cfg: %s id=%lu reboot=%lu sn=%s",
             profile->name,
             (unsigned long)profile->product_id,
             (unsigned long)n->boot_count,
             n->serial);
}

int32_t cfg_motor_rpm(uint8_t motor_id, int32_t rpm)
{
    const nvs_cfg_t *n = nvs_cfg_get();
    u32_t bit;

    if ((motor_id < 1U) || (motor_id > 4U)) {
        return rpm;
    }

    bit = 1U << (motor_id - 1U);
    if ((n->motor_dir_mask & bit) != 0U) {
        rpm = -rpm;
    }

    return rpm;
}

int32_t cfg_encoder_count(uint8_t index)
{
    const nvs_cfg_t *n = nvs_cfg_get();
    int32_t raw = Encoder_GetCount(index);

    if (index < NVS_CFG_ENCODER_MAX) {
        raw -= n->encoder_zero.zero[index];
    }

    return raw;
}

int32_t cfg_encoder_delta(uint8_t wheel_index, int32_t delta)
{
    const nvs_cfg_t *n = nvs_cfg_get();
    u32_t bit;

    if (wheel_index >= NVS_CFG_ENCODER_MAX) {
        return delta;
    }

    bit = 1U << wheel_index;
    if ((n->encoder_dir_mask & bit) != 0U) {
        return -delta;
    }
    return delta;
}

uint16_t cfg_line_threshold(uint8_t sensor_index)
{
    const nvs_cfg_t *n = nvs_cfg_get();

    if (sensor_index >= NVS_CFG_LINE_SENSOR_COUNT) {
        return 2048U;
    }

    return n->line_threshold.threshold[sensor_index];
}

uint32_t cfg_battery_calibrate_mv(uint32_t raw_mv)
{
    const nvs_cfg_t *n = nvs_cfg_get();
    f32_t mv = (f32_t)raw_mv * n->battery_cal.scale;

    mv += n->battery_cal.offset_v * 1000.0f;
    if (mv < 0.0f) {
        return 0U;
    }
    return (uint32_t)mv;
}

const nvs_spd_limit_t *cfg_spd_limit(void)
{
    return &nvs_cfg_get()->spd_limit;
}

const nvs_kinematics_t *cfg_kinematics(void)
{
    return &nvs_cfg_get()->kinematics;
}

const nvs_pid3_t *cfg_pid_speed(void)
{
    return &nvs_cfg_get()->pid_speed;
}

const nvs_pid3_t *cfg_pid_line(void)
{
    return &nvs_cfg_get()->pid_line;
}
