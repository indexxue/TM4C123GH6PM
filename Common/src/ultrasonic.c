/**
 * @file    ultrasonic.c
 * @brief   HC-SR04 超声波测距公共模块（cbb/hc_sr04 + 板级 GPIO 适配）
 */

#include "ultrasonic.h"

#include "board.h"
#include "bsp_gpio.h"
#include "bsp_systick.h"
#include "hc_sr04.h"

static const bsp_gpio_pin_t s_trig = { GPIO_ULTRA_TRIG_PORT, GPIO_ULTRA_TRIG_MASK };
static const bsp_gpio_pin_t s_echo = { GPIO_ULTRA_ECHO_PORT, GPIO_ULTRA_ECHO_MASK };

static hc_sr04_t s_dev;
static bool_t s_ready;

static void ultrasonic_gpio_set(uint8_t pin_id, uint8_t level)
{
    if (pin_id == HC_SR04_PIN_TRIG) {
        bsp_gpio_write(&s_trig, level != 0u);
    }
}

static uint8_t ultrasonic_gpio_read(uint8_t pin_id)
{
    if (pin_id == HC_SR04_PIN_ECHO) {
        return bsp_gpio_read(&s_echo) ? 1u : 0u;
    }
    return 0u;
}

static status_t ultrasonic_status_from_hc(hc_sr04_status_t st)
{
    switch (st) {
    case HC_SR04_OK:
        return STATUS_OK;
    case HC_SR04_ERROR_PARAM:
        return STATUS_INVALID_ARG;
    case HC_SR04_ERROR_NOT_INIT:
        return STATUS_INVALID_STATE;
    case HC_SR04_ERROR_TIMEOUT:
        return STATUS_TIMEOUT;
    case HC_SR04_ERROR_OUT_OF_RANGE:
        return STATUS_FAIL;
    default:
        return STATUS_FAIL;
    }
}

status_t ultrasonic_init(void)
{
    hc_sr04_config_t cfg = {0};
    hc_sr04_status_t st;

    if (s_ready != FALSE) {
        return STATUS_OK;
    }

    if (!Board_Ultra_Init()) {
        return STATUS_FAIL;
    }

    cfg.gpio_set    = ultrasonic_gpio_set;
    cfg.gpio_read   = ultrasonic_gpio_read;
    cfg.delay_us    = bsp_delay_us;
    cfg.get_tick_us = bsp_get_tick_us;

    st = hc_sr04_init_with_config(&s_dev, &cfg);
    if (st != HC_SR04_OK) {
        return ultrasonic_status_from_hc(st);
    }

    s_ready = TRUE;
    return STATUS_OK;
}

bool_t ultrasonic_is_ready(void)
{
    return s_ready;
}

status_t ultrasonic_measure_mm(uint16_t *distance_mm)
{
    if (s_ready == FALSE) {
        return STATUS_INVALID_STATE;
    }
    if (distance_mm == NULL) {
        return STATUS_INVALID_ARG;
    }

    return ultrasonic_status_from_hc(hc_sr04_measure_mm(&s_dev, distance_mm));
}

status_t ultrasonic_measure_cm(uint8_t *distance_cm)
{
    if (s_ready == FALSE) {
        return STATUS_INVALID_STATE;
    }
    if (distance_cm == NULL) {
        return STATUS_INVALID_ARG;
    }

    return ultrasonic_status_from_hc(hc_sr04_measure_cm(&s_dev, distance_cm));
}
