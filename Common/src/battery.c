/**
 * @file battery.c
 * @brief TM4C123 电池电压采样（ADC1 AIN0 @ PE3，分压比 2）
 */

#include "battery.h"

#include <string.h>

#include "board.h"
#include "bsp_adc.h"

#include "FreeRTOS.h"
#include "task.h"

#define BATTERY_SAMPLE_CNT (8U)
#define BATTERY_DIVIDER_RATIO (2U)
#define BATTERY_MV_EMPTY (3300U)
#define BATTERY_MV_FULL_SOC (4100U)
#define BATTERY_MV_CHARGING (4200U)
#define BATTERY_LEVEL_PERCENT_VALUES 20, 50, 80, 100
#define BATTERY_SAMPLE_PERIOD_MS (120000U)

static const uint8_t s_level_percent[BATTERY_LEVEL_NUM] = {BATTERY_LEVEL_PERCENT_VALUES};

static TickType_t s_last_hw_sample_ticks;
static bool_t s_hw_sample_cache_valid;
static uint32_t s_cached_vbatt_mv;
static battery_voltage_t s_cached_voltage;

static struct {
    bool_t initialized;
    battery_voltage_t voltage;
    battery_info_t info;
    bool_t data_valid;
} s_self;

static uint32_t battery_read_adc_raw(void)
{
    uint32_t value = 0U;

    if (!bsp_adc_sample_one(&BOARD_BATTERY_ADC_CFG, &value)) {
        return 0U;
    }

    return value;
}

static uint8_t mv_to_percent(uint32_t mv)
{
    if (mv >= BATTERY_MV_FULL_SOC) {
        return 100U;
    }
    if (mv <= BATTERY_MV_EMPTY) {
        return 0U;
    }
    return (uint8_t)((mv - BATTERY_MV_EMPTY) * 100U / (BATTERY_MV_FULL_SOC - BATTERY_MV_EMPTY));
}

void battery_init(void)
{
    if (s_self.initialized != FALSE) {
        return;
    }
    (void)memset(&s_self, 0, sizeof(s_self));
    s_hw_sample_cache_valid = FALSE;
    s_last_hw_sample_ticks = 0;
    s_self.initialized = TRUE;
}

static uint32_t battery_voltage_sample_hw(battery_voltage_t *voltage)
{
    uint32_t sum = 0U;
    uint32_t min_raw = 0U;
    uint32_t max_raw = 0U;
    uint16_t count = 0U;

    for (uint16_t i = 0U; i < BATTERY_SAMPLE_CNT; i++) {
        uint32_t raw = battery_read_adc_raw();
        if (raw == 0U) {
            return 0U;
        }

        sum += raw;
        if (count == 0U) {
            min_raw = max_raw = raw;
            count = 1U;
            continue;
        }
        count++;
        if (raw < min_raw) {
            min_raw = raw;
        }
        if (raw > max_raw) {
            max_raw = raw;
        }
    }

    if (count < 3U) {
        return 0U;
    }

    sum -= min_raw;
    sum -= max_raw;
    count = (uint16_t)((uint32_t)count - 2U);

    {
        uint32_t avg_raw = sum / (uint32_t)count;
        uint32_t vadc_mv = (avg_raw * 3300U) / 4095U;
        uint32_t vbatt_mv = vadc_mv * BATTERY_DIVIDER_RATIO;

        if (voltage != NULL) {
            voltage->current_mv = (uint16_t)vbatt_mv;
            voltage->min_mv = (uint16_t)((min_raw * 3300U * BATTERY_DIVIDER_RATIO) / 4095U);
            voltage->max_mv = (uint16_t)((max_raw * 3300U * BATTERY_DIVIDER_RATIO) / 4095U);
        }
        return vbatt_mv;
    }
}

uint32_t battery_voltage_read_mv(battery_voltage_t *voltage)
{
    TickType_t now;
    TickType_t period_ticks;

    if (s_self.initialized == FALSE) {
        return 0U;
    }

    now = xTaskGetTickCount();
    period_ticks = pdMS_TO_TICKS(BATTERY_SAMPLE_PERIOD_MS);
    if (period_ticks == 0U) {
        period_ticks = 1U;
    }

    if ((s_hw_sample_cache_valid != FALSE) &&
        ((TickType_t)(now - s_last_hw_sample_ticks) < period_ticks)) {
        if (voltage != NULL) {
            *voltage = s_cached_voltage;
        }
        return s_cached_vbatt_mv;
    }

    {
        battery_voltage_t v_local;
        battery_voltage_t *vout = (voltage != NULL) ? voltage : &v_local;
        uint32_t vbatt_mv = battery_voltage_sample_hw(vout);

        if (vbatt_mv == 0U) {
            return 0U;
        }

        s_cached_vbatt_mv = vbatt_mv;
        s_cached_voltage = *vout;
        s_last_hw_sample_ticks = now;
        s_hw_sample_cache_valid = TRUE;
        return vbatt_mv;
    }
}

bool_t battery_percent_update(void)
{
    battery_voltage_t v;
    uint32_t mv;

    if (s_self.initialized == FALSE) {
        return FALSE;
    }

    mv = battery_voltage_read_mv(&v);
    if (mv == 0U) {
        return FALSE;
    }

    s_self.voltage = v;
    s_self.info.percent = mv_to_percent(mv);
    s_self.info.charging = (mv >= BATTERY_MV_CHARGING) ? TRUE : FALSE;
    s_self.info.level = BATTERY_LEVEL_NUM - 1U;
    for (uint8_t i = 0U; i < BATTERY_LEVEL_NUM; i++) {
        if (s_self.info.percent < s_level_percent[i]) {
            s_self.info.level = i;
            break;
        }
    }
    s_self.data_valid = TRUE;
    return TRUE;
}

bool_t battery_info_read(battery_info_t *info, battery_voltage_t *voltage)
{
    if ((s_self.initialized == FALSE) || (s_self.data_valid == FALSE)) {
        return FALSE;
    }
    if (info != NULL) {
        *info = s_self.info;
    }
    if (voltage != NULL) {
        *voltage = s_self.voltage;
    }
    return TRUE;
}
