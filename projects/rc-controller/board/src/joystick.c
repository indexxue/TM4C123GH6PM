/**
 * @file    joystick.c
 * @brief   双操纵杆 ADC/GPIO 采样与轴向映射（支持运行时校准表）
 */

#include "joystick.h"

#include "board.h"

#include "bsp_adc.h"

#include "driverlib/gpio.h"

#include <string.h>

static bool_t s_ready;
static board_joystick_axis_cfg_t s_axis_cfg[BOARD_JOYSTICK_AXIS_COUNT];

static const board_joystick_axis_cfg_t s_axis_cfg_default = {
    .adc_min = BOARD_JOY_ADC_MIN,
    .adc_center = BOARD_JOY_ADC_CENTER,
    .adc_max = BOARD_JOY_ADC_MAX,
    .deadband = BOARD_JOY_ADC_DEADBAND,
    .edge_margin = BOARD_JOY_ADC_EDGE_MARGIN,
    .cmd_max = BOARD_JOYSTICK_CMD_MAX,
    .invert = FALSE,
};

status_t board_joystick_init(void)
{
    size_t i;

    for (i = 0U; i < BOARD_JOYSTICK_AXIS_COUNT; i++) {
        s_axis_cfg[i] = s_axis_cfg_default;
    }

    s_ready = bsp_adc_init(&BOARD_JOY_ADC_CFG);
    return s_ready ? STATUS_OK : STATUS_FAIL;
}

bool_t board_joystick_is_ready(void)
{
    return s_ready;
}

void board_joystick_axis_cfg_default(board_joystick_axis_cfg_t *cfg)
{
    if (cfg != NULL) {
        *cfg = s_axis_cfg_default;
    }
}

void board_joystick_set_axis_cfg(board_joystick_axis_id_t axis,
                                 const board_joystick_axis_cfg_t *cfg)
{
    if ((cfg == NULL) || ((uint32_t)axis >= BOARD_JOYSTICK_AXIS_COUNT)) {
        return;
    }
    s_axis_cfg[axis] = *cfg;
}

void board_joystick_get_axis_cfg(board_joystick_axis_id_t axis,
                                 board_joystick_axis_cfg_t *cfg)
{
    if ((cfg == NULL) || ((uint32_t)axis >= BOARD_JOYSTICK_AXIS_COUNT)) {
        return;
    }
    *cfg = s_axis_cfg[axis];
}

int16_t board_joystick_map_axis(const board_joystick_axis_cfg_t *cfg, uint16_t adc_raw,
                                bool_t *in_deadband)
{
    int32_t delta;
    int32_t span;
    int32_t eff_raw;
    uint16_t lo;
    uint16_t hi;

    if (cfg == NULL) {
        return 0;
    }

    lo = (uint16_t)(cfg->adc_min + cfg->edge_margin);
    hi = (uint16_t)(cfg->adc_max - cfg->edge_margin);
    if (lo >= hi) {
        lo = cfg->adc_min;
        hi = cfg->adc_max;
    }

    eff_raw = (int32_t)adc_raw;
    if (eff_raw < (int32_t)lo) {
        eff_raw = (int32_t)lo;
    } else if (eff_raw > (int32_t)hi) {
        eff_raw = (int32_t)hi;
    }

    delta = eff_raw - (int32_t)cfg->adc_center;
    if (cfg->invert) {
        delta = -delta;
    }

    if ((delta > -(int32_t)cfg->deadband) && (delta < (int32_t)cfg->deadband)) {
        if (in_deadband != NULL) {
            *in_deadband = TRUE;
        }
        return 0;
    }

    if (in_deadband != NULL) {
        *in_deadband = FALSE;
    }

    if (delta > 0) {
        delta -= (int32_t)cfg->deadband;
        span = (int32_t)hi - (int32_t)cfg->adc_center - (int32_t)cfg->deadband;
        if ((span <= 0) || (delta <= 0)) {
            return 0;
        }
        if (delta > span) {
            delta = span;
        }
        return (int16_t)((delta * (int32_t)cfg->cmd_max) / span);
    }

    delta += (int32_t)cfg->deadband;
    span = (int32_t)cfg->adc_center - (int32_t)lo - (int32_t)cfg->deadband;
    if ((span <= 0) || (delta >= 0)) {
        return 0;
    }
    delta = -delta;
    if (delta > span) {
        delta = span;
    }
    return (int16_t)(-((delta * (int32_t)cfg->cmd_max) / span));
}

static void board_joystick_map_one(board_joystick_state_t *state,
                                   const board_joystick_axis_cfg_t *cfg_x,
                                   const board_joystick_axis_cfg_t *cfg_y)
{
    if (state == NULL) {
        return;
    }

    state->mapped.x_cmd = board_joystick_map_axis(cfg_x, state->raw.x_raw, &state->mapped.x_in_deadband);
    state->mapped.y_cmd = board_joystick_map_axis(cfg_y, state->raw.y_raw, &state->mapped.y_in_deadband);
    state->mapped.btn_pressed = state->raw.btn_pressed;
}

bool_t board_joystick_sample(board_joystick_state_t out[BOARD_JOYSTICK_COUNT])
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
        out[i].raw.x_raw = (uint16_t)(raw[i * 2U] & 0xFFFFU);
        out[i].raw.y_raw = (uint16_t)(raw[(i * 2U) + 1U] & 0xFFFFU);
        out[i].raw.btn_pressed = FALSE;
    }

    out[BOARD_JOYSTICK_1].raw.btn_pressed =
        (GPIOPinRead(GPIO_JS1_BTN_PORT, GPIO_JS1_BTN_MASK) == 0U) ? TRUE : FALSE;
    out[BOARD_JOYSTICK_2].raw.btn_pressed =
        (GPIOPinRead(GPIO_JS2_BTN_PORT, GPIO_JS2_BTN_MASK) == 0U) ? TRUE : FALSE;

    board_joystick_map_one(&out[BOARD_JOYSTICK_1],
                           &s_axis_cfg[BOARD_JOY_AXIS_J1X],
                           &s_axis_cfg[BOARD_JOY_AXIS_J1Y]);
    board_joystick_map_one(&out[BOARD_JOYSTICK_2],
                           &s_axis_cfg[BOARD_JOY_AXIS_J2X],
                           &s_axis_cfg[BOARD_JOY_AXIS_J2Y]);

    return TRUE;
}

bool_t board_joystick_sample_drive(board_joystick_drive_t *drive)
{
    board_joystick_state_t js[BOARD_JOYSTICK_COUNT];

    if (!board_joystick_sample(js)) {
        return FALSE;
    }

    if (drive != NULL) {
        drive->throttle = js[BOARD_JOYSTICK_1].mapped.y_cmd;
        drive->steer = js[BOARD_JOYSTICK_1].mapped.x_cmd;
        drive->aux = js[BOARD_JOYSTICK_2].mapped;
    }

    return TRUE;
}
