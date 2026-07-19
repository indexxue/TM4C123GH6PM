/**
 * @file    line_follow.c
 * @brief   6 路 ADC 循迹偏差与外环 PID
 */

#include "line_follow.h"

#include "board.h"
#include "cfg.h"
#include "device_profile.h"
#include "pid.h"

#include <stddef.h>

#ifndef LINE_FOLLOW_PID_OUT_MAX_RPM
#define LINE_FOLLOW_PID_OUT_MAX_RPM  150.0f
#endif

#ifndef LINE_FOLLOW_PID_INTEGRAL_MAX
#define LINE_FOLLOW_PID_INTEGRAL_MAX  80.0f
#endif

/* 丢线短时搜索：沿上次偏差方向差速找线，避免急弯冲出全白立刻刹停 */
#ifndef LINE_FOLLOW_SEARCH_BASE_RPM
#define LINE_FOLLOW_SEARCH_BASE_RPM  35.0f
#endif

#ifndef LINE_FOLLOW_SEARCH_TURN_RPM
#define LINE_FOLLOW_SEARCH_TURN_RPM  90.0f
#endif

/* 物理左→右：PD3 PD2 PD1 PD0 PE5 PE4（LINE1…LINE6）
 * 中线：PD1/PD0（LINE3/LINE4），权重 −1/+1；两者同时压线时 error≈0 */
static const f32_t s_weights[NVS_CFG_LINE_SENSOR_COUNT] = {
    -3.0f, -2.0f, -1.0f, 1.0f, 2.0f, 3.0f
};

static pid_t s_pid_line;
static f32_t s_error;
static f32_t s_last_good_error;
static f32_t s_base_rpm;
static f32_t s_left_rpm;
static f32_t s_right_rpm;
static f32_t s_turn_rpm;
static line_follow_state_t s_state;
static uint8_t s_detect_mask;
static u16_t s_lost_frames;

void line_follow_init(void)
{
    const nvs_pid3_t *g = cfg_pid_line();

    pid_init(&s_pid_line,
             (g != NULL) ? g->kp : 2.0f,
             (g != NULL) ? g->ki : 0.0f,
             (g != NULL) ? g->kd : 0.1f);
    pid_set_output_limits(&s_pid_line, -LINE_FOLLOW_PID_OUT_MAX_RPM, LINE_FOLLOW_PID_OUT_MAX_RPM);
    pid_set_integral_limit(&s_pid_line, LINE_FOLLOW_PID_INTEGRAL_MAX);
    line_follow_reset();
    line_follow_reload_pid();
}

void line_follow_reset(void)
{
    pid_reset(&s_pid_line);
    s_error = 0.0f;
    s_last_good_error = 0.0f;
    s_base_rpm = cfg_line_base_rpm();
    s_left_rpm = 0.0f;
    s_right_rpm = 0.0f;
    s_turn_rpm = 0.0f;
    s_state = LINE_FOLLOW_STATE_IDLE;
    s_detect_mask = 0U;
    s_lost_frames = 0U;
}

void line_follow_reload_pid(void)
{
    const nvs_pid3_t *g = cfg_pid_line();
    const nvs_spd_limit_t *lim = cfg_spd_limit();
    f32_t out_max;

    if (g != NULL) {
        pid_set_gains(&s_pid_line, g->kp, g->ki, g->kd);
    }

    out_max = LINE_FOLLOW_PID_OUT_MAX_RPM;
    if ((lim != NULL) && (lim->max_rpm > 1.0f)) {
        out_max = lim->max_rpm * 0.5f;
    }
    pid_set_output_limits(&s_pid_line, -out_max, out_max);
}

void line_follow_set_base_rpm(f32_t base_rpm)
{
    s_base_rpm = base_rpm;
}

uint8_t line_follow_mask_from_adc(const uint16_t *adc, size_t count)
{
    uint8_t mask = 0U;
    size_t n;
    size_t i;

    if (adc == NULL) {
        return 0U;
    }

    n = (count < NVS_CFG_LINE_SENSOR_COUNT) ? count : NVS_CFG_LINE_SENSOR_COUNT;
    for (i = 0U; i < n; i++) {
        if (cfg_line_is_black((uint8_t)i, adc[i])) {
            mask |= (uint8_t)(1U << i);
        }
    }
    return mask;
}

static f32_t line_follow_compute_error(const uint16_t *adc, size_t count, uint8_t *mask_out)
{
    f32_t num = 0.0f;
    f32_t den = 0.0f;
    size_t n;
    size_t i;
    uint8_t mask;

    mask = line_follow_mask_from_adc(adc, count);
    if (mask_out != NULL) {
        *mask_out = mask;
    }

    n = (count < NVS_CFG_LINE_SENSOR_COUNT) ? count : NVS_CFG_LINE_SENSOR_COUNT;
    for (i = 0U; i < n; i++) {
        if ((mask & (uint8_t)(1U << i)) != 0U) {
            num += s_weights[i];
            den += 1.0f;
        }
    }

    if (den <= 0.0f) {
        return 0.0f;
    }
    return num / den;
}

void line_follow_update(f32_t dt_s, f32_t *left_rpm_out, f32_t *right_rpm_out)
{
    uint16_t adc[NVS_CFG_LINE_SENSOR_COUNT];
    f32_t turn;
    f32_t left;
    f32_t right;
    f32_t base;
    size_t i;

    if ((left_rpm_out == NULL) || (right_rpm_out == NULL)) {
        return;
    }

    *left_rpm_out = 0.0f;
    *right_rpm_out = 0.0f;
    s_left_rpm = 0.0f;
    s_right_rpm = 0.0f;
    s_turn_rpm = 0.0f;

    for (i = 0U; i < NVS_CFG_LINE_SENSOR_COUNT; i++) {
        adc[i] = 0U;
    }

    if (!device_profile_board_wants(DEVICE_BOARD_MASK_LINE) || !Line_IsReady()) {
        s_state = LINE_FOLLOW_STATE_LOST;
        s_detect_mask = 0U;
        s_error = 0.0f;
        return;
    }

    (void)Line_Sample(adc, NVS_CFG_LINE_SENSOR_COUNT);
    s_error = line_follow_compute_error(adc, NVS_CFG_LINE_SENSOR_COUNT, &s_detect_mask);

    if (s_detect_mask == 0U) {
        f32_t sign;

        if (s_lost_frames < 0xFFFFU) {
            s_lost_frames++;
        }
        s_state = LINE_FOLLOW_STATE_LOST;

        /* 从未见过线：直接停；否则沿上次偏差方向短时搜索 */
        if ((s_last_good_error > -0.05f) && (s_last_good_error < 0.05f)) {
            return;
        }

        sign = (s_last_good_error >= 0.0f) ? 1.0f : -1.0f;
        /* error>0（线在右）→ turn<0 → 左快右慢 → 右转找线 */
        turn = -sign * LINE_FOLLOW_SEARCH_TURN_RPM;
        left = LINE_FOLLOW_SEARCH_BASE_RPM - turn;
        right = LINE_FOLLOW_SEARCH_BASE_RPM + turn;
        s_error = s_last_good_error;
        s_turn_rpm = turn;
        s_left_rpm = left;
        s_right_rpm = right;
        *left_rpm_out = left;
        *right_rpm_out = right;
        return;
    }

    s_lost_frames = 0U;
    s_state = LINE_FOLLOW_STATE_TRACK;
    s_last_good_error = s_error;

    base = s_base_rpm;
    if (base <= 0.0f) {
        base = cfg_line_base_rpm();
    }

    if (dt_s <= 0.0f) {
        dt_s = 0.02f;
    }

    /* setpoint=0，measurement=error → turn=-Kp*error；线偏右时右转差速修正 */
    turn = pid_update(&s_pid_line, 0.0f, s_error, dt_s);
    left = base - turn;
    right = base + turn;

    s_turn_rpm = turn;
    s_left_rpm = left;
    s_right_rpm = right;
    *left_rpm_out = left;
    *right_rpm_out = right;
}

f32_t line_follow_get_error(void)
{
    return s_error;
}

line_follow_state_t line_follow_get_state(void)
{
    return s_state;
}

uint8_t line_follow_get_detect_mask(void)
{
    return s_detect_mask;
}

u16_t line_follow_get_lost_frames(void)
{
    return s_lost_frames;
}

f32_t line_follow_get_left_rpm(void)
{
    return s_left_rpm;
}

f32_t line_follow_get_right_rpm(void)
{
    return s_right_rpm;
}

f32_t line_follow_get_turn_rpm(void)
{
    return s_turn_rpm;
}
