/**
 * @file    joystick.c
 * @brief   操纵杆 ADC/GPIO（BOARD_JOY_ADC_CFG + board.h 按键脚）
 */

#include "joystick.h"

#include "board.h"
#include "proto_client.h"

#include "bsp_adc.h"

#include "driverlib/gpio.h"

#define BOARD_JOY_ADC_CENTER       2048U
#define BOARD_JOY_ADC_DEADBAND     80U
#define BOARD_JOY_ADC_SPAN         1800U

static bool_t s_ready;

status_t board_joystick_init(void)
{
    s_ready = bsp_adc_init(&BOARD_JOY_ADC_CFG);
    return s_ready ? STATUS_OK : STATUS_FAIL;
}

bool_t board_joystick_sample(board_joystick_sample_t out[BOARD_JOYSTICK_COUNT])
{
    uint32_t raw[4];
    size_t i;

    if ((out == NULL) || !s_ready) {
        return FALSE;
    }

    if (!bsp_adc_sample(&BOARD_JOY_ADC_CFG, raw, 4U)) {
        return FALSE;
    }

    for (i = 0U; i < BOARD_JOYSTICK_COUNT; i++) {
        out[i].x = (uint16_t)(raw[i * 2U] & 0xFFFFU);
        out[i].y = (uint16_t)(raw[(i * 2U) + 1U] & 0xFFFFU);
        out[i].btn_pressed = FALSE;
    }

    out[0].btn_pressed = (GPIOPinRead(GPIO_JS1_BTN_PORT, GPIO_JS1_BTN_MASK) == 0U) ? TRUE : FALSE;
    out[1].btn_pressed = (GPIOPinRead(GPIO_JS2_BTN_PORT, GPIO_JS2_BTN_MASK) == 0U) ? TRUE : FALSE;
    return TRUE;
}

int16_t board_joystick_axis_to_cmd(uint16_t adc_raw)
{
    int32_t delta = (int32_t)adc_raw - (int32_t)BOARD_JOY_ADC_CENTER;

    if ((delta > -(int32_t)BOARD_JOY_ADC_DEADBAND) && (delta < (int32_t)BOARD_JOY_ADC_DEADBAND)) {
        return 0;
    }
    if (delta > (int32_t)BOARD_JOY_ADC_SPAN) {
        delta = (int32_t)BOARD_JOY_ADC_SPAN;
    } else if (delta < -(int32_t)BOARD_JOY_ADC_SPAN) {
        delta = -(int32_t)BOARD_JOY_ADC_SPAN;
    }
    return (int16_t)((delta * PROTO_CLIENT_DRIVE_THROTTLE_MAX) / (int32_t)BOARD_JOY_ADC_SPAN);
}
