/**
 * @file bsp_dac.c
 * @brief Timer PWM 软 DAC（TM4C123GH6PM 无硬件 DAC 外设）
 */

#include "bsp_dac.h"

#include "bsp_config.h"

bool bsp_dac_init(const bsp_dac_config_t *cfg)
{
    bsp_pwm_config_t pwm_cfg;

    if ((cfg == NULL) || (cfg->pwm.timer_base == 0U) || (cfg->pwm.frequency_hz == 0U) || (cfg->clock_hz == 0U)) {
        return false;
    }

    pwm_cfg = (bsp_pwm_config_t){
        .channels = &cfg->pwm,
        .channel_count = 1U,
        .clock_hz = cfg->clock_hz,
    };
    return bsp_pwm_init(&pwm_cfg);
}

void bsp_dac_set_value(const bsp_dac_config_t *cfg, uint32_t value)
{
    uint32_t max_value;
    uint16_t duty_permille;

    if ((cfg == NULL) || (cfg->pwm.timer_base == 0U)) {
        return;
    }

    if (cfg->resolution_bits == 0U) {
        max_value = 255U;
    } else if (cfg->resolution_bits >= 16U) {
        max_value = 65535U;
    } else {
        max_value = (1U << cfg->resolution_bits) - 1U;
    }

    if (value > max_value) {
        value = max_value;
    }

    duty_permille = (uint16_t)((value * 1000U) / max_value);
    bsp_pwm_set_duty(cfg->pwm.timer_base, cfg->pwm.channel, duty_permille);
}
