/* Auto-generated line module — edit .syscfg, not this file */
#include <stdbool.h>
#include <stdint.h>
#include "driverlib/gpio.h"
#include "driverlib/pin_map.h"
#include "driverlib/sysctl.h"
#include "inc/hw_memmap.h"
#include <stddef.h>
#include "bsp_adc.h"
#include "bsp_uart.h"
#include "board.h"

#define LINE_SENSOR_COUNT 5U

static bool s_line_adc_ready;

bool Line_IsReady(void)
{
    return s_line_adc_ready;
}

bool Line_Sample(uint16_t *out, size_t count)
{
    uint32_t raw[LINE_SENSOR_COUNT];
    size_t i;
    size_t n = (count < LINE_SENSOR_COUNT) ? count : LINE_SENSOR_COUNT;

    if ((out == NULL) || !s_line_adc_ready) {
        return false;
    }
    for (i = 0U; i < count; i++) {
        out[i] = 0U;
    }
    if (!bsp_adc_sample(&BOARD_LINE_ADC_CFG, raw, LINE_SENSOR_COUNT)) {
        return false;
    }
    for (i = 0U; i < n; i++) {
        out[i] = (uint16_t)(raw[i] & 0xFFFFU);
    }
    return true;
}

void Line_Init(void) {
    uint32_t raw[LINE_SENSOR_COUNT];

    s_line_adc_ready = bsp_adc_init(&BOARD_LINE_ADC_CFG);
    if (!s_line_adc_ready) {
        bsp_uart_debug_puts("[line] adc init FAIL\r\n");
        return;
    }
    if (!bsp_adc_sample(&BOARD_LINE_ADC_CFG, raw, LINE_SENSOR_COUNT)) {
        bsp_uart_debug_puts("[line] adc boot sample FAIL\r\n");
        s_line_adc_ready = false;
        return;
    }
    bsp_uart_debug_puts("[line] adc boot sample OK\r\n");
}