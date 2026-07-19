/**
 * @file bsp_adc.c
 * @brief TM4C123 ADC 处理器触发序列采样
 */

#include "bsp_adc.h"

#include "bsp_systick.h"
#include "bsp_sysctl.h"

#include "bsp_config.h"

#include "driverlib/adc.h"
#include "driverlib/sysctl.h"
#include "inc/hw_memmap.h"

#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

#define BSP_ADC_PERIPH_READY_US 100000U
#define BSP_ADC_SAMPLE_TIMEOUT_US 100000U
#define BSP_ADC_LOCK_TIMEOUT_MS 50U

static SemaphoreHandle_t s_adc1_mutex;
static bool s_adc0_clock_configured;
static bool s_adc1_clock_configured;

static uint32_t adc_periph_from_base(uint32_t base)
{
    switch (base) {
    case ADC0_BASE:
        return SYSCTL_PERIPH_ADC0;
    case ADC1_BASE:
        return SYSCTL_PERIPH_ADC1;
    default:
        return 0U;
    }
}

static void adc1_mutex_init(void)
{
    if (s_adc1_mutex == NULL) {
        s_adc1_mutex = xSemaphoreCreateMutex();
    }
}

static bool adc_lock(uint32_t base)
{
    if (base != ADC1_BASE) {
        return true;
    }

    adc1_mutex_init();
    if (s_adc1_mutex == NULL) {
        return true;
    }

    if (xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED) {
        return true;
    }

    return xSemaphoreTake(s_adc1_mutex, pdMS_TO_TICKS(BSP_ADC_LOCK_TIMEOUT_MS)) == pdTRUE;
}

static void adc_unlock(uint32_t base)
{
    if ((base != ADC1_BASE) || (s_adc1_mutex == NULL)) {
        return;
    }

    if (xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED) {
        return;
    }

    (void)xSemaphoreGive(s_adc1_mutex);
}

static uint32_t adc_ctl_for_channel(uint8_t channel)
{
    static const uint32_t ctl_table[] = {
        ADC_CTL_CH0,  ADC_CTL_CH1,  ADC_CTL_CH2,  ADC_CTL_CH3,
        ADC_CTL_CH4,  ADC_CTL_CH5,  ADC_CTL_CH6,  ADC_CTL_CH7,
        ADC_CTL_CH8,  ADC_CTL_CH9,  ADC_CTL_CH10, ADC_CTL_CH11,
        ADC_CTL_CH12, ADC_CTL_CH13, ADC_CTL_CH14, ADC_CTL_CH15,
        ADC_CTL_CH16, ADC_CTL_CH17, ADC_CTL_CH18, ADC_CTL_CH19,
        ADC_CTL_CH20, ADC_CTL_CH21, ADC_CTL_CH22, ADC_CTL_CH23,
    };

    if (channel >= (sizeof(ctl_table) / sizeof(ctl_table[0]))) {
        return ADC_CTL_CH0;
    }

    return ctl_table[channel];
}

static void adc_module_clock_init(uint32_t base)
{
    if (base == ADC0_BASE) {
        if (!s_adc0_clock_configured) {
            ADCClockConfigSet(ADC0_BASE, ADC_CLOCK_SRC_PLL | ADC_CLOCK_RATE_FULL, 4U);
            s_adc0_clock_configured = true;
        }
        return;
    }

    if ((base == ADC1_BASE) && !s_adc1_clock_configured) {
        ADCClockConfigSet(ADC1_BASE, ADC_CLOCK_SRC_PLL | ADC_CLOCK_RATE_FULL, 4U);
        s_adc1_clock_configured = true;
    }
}

static void adc_fifo_flush(uint32_t base, uint32_t sequence)
{
    uint32_t junk[8];

    /* 一次读空 FIFO，避免残留样本导致通道错位（首/末通道表现为“不变”） */
    (void)ADCSequenceDataGet(base, sequence, junk);
    if (ADCSequenceOverflow(base, sequence) != 0) {
        ADCSequenceOverflowClear(base, sequence);
    }
}

static void adc_sequence_recover(const bsp_adc_config_t *cfg)
{
    ADCIntClear(cfg->base, cfg->sequence);
    adc_fifo_flush(cfg->base, cfg->sequence);
    ADCSequenceDisable(cfg->base, cfg->sequence);
    ADCSequenceEnable(cfg->base, cfg->sequence);
}

bool bsp_adc_init(const bsp_adc_config_t *cfg)
{
    uint32_t periph;
    size_t i;

    if ((cfg == NULL) || (cfg->base == 0U) || (cfg->channels == NULL) || (cfg->channel_count == 0U)) {
        return false;
    }

    periph = adc_periph_from_base(cfg->base);
    if (periph == 0U) {
        return false;
    }

    if (cfg->base == ADC1_BASE) {
        adc1_mutex_init();
    }

    SysCtlPeripheralEnable(periph);
    if (!bsp_periph_wait_ready(periph, BSP_ADC_PERIPH_READY_US)) {
        return false;
    }

    adc_module_clock_init(cfg->base);
    ADCReferenceSet(cfg->base, ADC_REF_INT);
    ADCSequenceDisable(cfg->base, cfg->sequence);
    ADCSequenceConfigure(cfg->base, cfg->sequence, ADC_TRIGGER_PROCESSOR, 0);

    for (i = 0U; i < cfg->channel_count; i++) {
        uint32_t ctl = adc_ctl_for_channel(cfg->channels[i].channel);
        /* TM4C123 无独立 S&H 配置；勿 OR ADC_CTL_SHOLD_*（部分器件才有效） */
        if (i == (cfg->channel_count - 1U)) {
            ctl |= ADC_CTL_END | ADC_CTL_IE;
        }
        ADCSequenceStepConfigure(cfg->base, cfg->sequence, (uint32_t)i, ctl);
    }

    ADCSequenceEnable(cfg->base, cfg->sequence);
    adc_fifo_flush(cfg->base, cfg->sequence);
    ADCIntClear(cfg->base, cfg->sequence);
    return true;
}

bool bsp_adc_sample(const bsp_adc_config_t *cfg, uint32_t *values, size_t count)
{
    bsp_timeout_t timeout;
    int32_t got;
    size_t i;

    if ((cfg == NULL) || (values == NULL) || (count < cfg->channel_count)) {
        return false;
    }

    if (!adc_lock(cfg->base)) {
        return false;
    }

    for (i = 0U; i < cfg->channel_count; i++) {
        values[i] = 0U;
    }

    adc_fifo_flush(cfg->base, cfg->sequence);
    ADCIntClear(cfg->base, cfg->sequence);
    ADCProcessorTrigger(cfg->base, cfg->sequence);
    bsp_timeout_start_us(&timeout, BSP_ADC_SAMPLE_TIMEOUT_US);
    while (!ADCIntStatus(cfg->base, cfg->sequence, false)) {
        if (bsp_timeout_expired(&timeout)) {
            adc_sequence_recover(cfg);
            adc_unlock(cfg->base);
            return false;
        }
        if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) {
            taskYIELD();
        }
    }

    ADCIntClear(cfg->base, cfg->sequence);
    got = ADCSequenceDataGet(cfg->base, cfg->sequence, values);
    if (got < (int32_t)cfg->channel_count) {
        adc_sequence_recover(cfg);
        adc_unlock(cfg->base);
        return false;
    }

    adc_unlock(cfg->base);
    return true;
}

bool bsp_adc_sample_one(const bsp_adc_config_t *cfg, uint32_t *value)
{
    uint32_t samples[4];

    if ((cfg == NULL) || (value == NULL)) {
        return false;
    }

    if (!bsp_adc_sample(cfg, samples, cfg->channel_count)) {
        return false;
    }

    *value = samples[0];
    return true;
}
