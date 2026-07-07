/**
 * @file bsp_adc.h
 * @brief TM4C123 ADC 序列采样薄封装
 */

#ifndef BSP_DRIVER_ADC_H
#define BSP_DRIVER_ADC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint8_t channel;
    uint8_t step;
} bsp_adc_channel_t;

typedef struct {
    uint32_t base;
    uint32_t sequence;
    const bsp_adc_channel_t *channels;
    size_t channel_count;
} bsp_adc_config_t;

bool bsp_adc_init(const bsp_adc_config_t *cfg);
bool bsp_adc_sample(const bsp_adc_config_t *cfg, uint32_t *values, size_t count);
bool bsp_adc_sample_one(const bsp_adc_config_t *cfg, uint32_t *value);

#endif /* BSP_DRIVER_ADC_H */
