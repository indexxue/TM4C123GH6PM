/**
 * @file bsp_sw_qei.c
 * @brief GPIO 轮询 + 4x 正交状态表软件编码器（RC 滤波板，纯 poll 不用边沿中断）
 */

#include "bsp_sw_qei.h"

#include <stddef.h>

#include "bsp_gpio.h"
#include "driverlib/gpio.h"
#include "driverlib/interrupt.h"
#include "inc/hw_memmap.h"

#define BSP_SW_QEI_MAX        4U
/** 每轮 poll_all 的最大解码步数（四路公平轮转，高速电机须足够大） */
#define BSP_SW_QEI_POLL_BURST 256U

typedef struct {
    bsp_sw_qei_channel_t ch;
    volatile int32_t count;
    uint8_t prev_state;
    int8_t last_step;
    bool used;
} bsp_sw_qei_state_t;

static bsp_sw_qei_state_t s_sw_qei[BSP_SW_QEI_MAX];

static const int8_t s_qdec_lut[16] = {
    0, +1, -1, 0, -1, 0, 0, +1, +1, 0, 0, -1, 0, -1, +1, 0,
};

static uint8_t sw_qei_read_state(const bsp_sw_qei_state_t *st)
{
    uint8_t a;
    uint8_t b;

    a = (GPIOPinRead(st->ch.pin_a.port_base, st->ch.pin_a.pin_mask) != 0U) ? 1U : 0U;
    b = (GPIOPinRead(st->ch.pin_b.port_base, st->ch.pin_b.pin_mask) != 0U) ? 1U : 0U;
    return (uint8_t)((a << 1U) | b);
}

static bool sw_qei_poll_one_step(bsp_sw_qei_state_t *st)
{
    uint8_t state = sw_qei_read_state(st);
    uint8_t delta = (uint8_t)(st->prev_state ^ state);
    uint8_t idx;
    int8_t step;

    if (delta == 0U) {
        return false;
    }

    /* A/B 同时翻转：漏采中间态，按上次有效方向补 1 步后重同步 */
    if (delta == 3U) {
        if (st->last_step != 0) {
            st->count += st->last_step;
        }
        st->prev_state = state;
        return true;
    }

    idx = (uint8_t)((st->prev_state << 2U) | state);
    step = s_qdec_lut[idx];
    st->prev_state = state;
    if (step != 0) {
        st->count += step;
        st->last_step = step;
    }

    return true;
}

static void sw_qei_poll_channel(bsp_sw_qei_state_t *st)
{
    uint16_t n;

    for (n = 0U; n < BSP_SW_QEI_POLL_BURST; n++) {
        if (!sw_qei_poll_one_step(st)) {
            break;
        }
    }
}

static void sw_qei_poll_all_fair(void)
{
    uint16_t round;
    uint8_t i;
    bool progressed;

    for (round = 0U; round < BSP_SW_QEI_POLL_BURST; round++) {
        progressed = false;
        for (i = 0U; i < BSP_SW_QEI_MAX; i++) {
            if (s_sw_qei[i].used && sw_qei_poll_one_step(&s_sw_qei[i])) {
                progressed = true;
            }
        }
        if (!progressed) {
            break;
        }
    }
}

bool bsp_sw_qei_register(uint8_t index, const bsp_sw_qei_channel_t *ch)
{
    bsp_sw_qei_state_t *st;

    if ((ch == NULL) || (index >= BSP_SW_QEI_MAX)) {
        return false;
    }

    st = &s_sw_qei[index];
    st->ch = *ch;
    st->count = 0;
    st->prev_state = sw_qei_read_state(st);
    st->last_step = 0;
    st->used = true;
    return true;
}

void bsp_sw_qei_enable(void)
{
    /*
     * RC 滤波 + 电机 PWM 噪声下 GPIO 边沿中断易误触发并打乱 prev_state，
     * 四路编码器统一纯轮询；引脚输入与上拉由 Encoder_Init 配置。
     */
}

int32_t bsp_sw_qei_get_count(uint8_t index)
{
    int32_t count;

    if (index >= BSP_SW_QEI_MAX) {
        return 0;
    }

    IntMasterDisable();
    count = s_sw_qei[index].count;
    IntMasterEnable();

    return count;
}

void bsp_sw_qei_poll(uint8_t index)
{
    if (index >= BSP_SW_QEI_MAX) {
        return;
    }
    if (!s_sw_qei[index].used) {
        return;
    }

    sw_qei_poll_channel(&s_sw_qei[index]);
}

void bsp_sw_qei_poll_all(void)
{
    sw_qei_poll_all_fair();
}

uint8_t bsp_sw_qei_read_ab(uint8_t index)
{
    if (index >= BSP_SW_QEI_MAX) {
        return 0U;
    }
    if (!s_sw_qei[index].used) {
        return 0U;
    }
    return sw_qei_read_state(&s_sw_qei[index]);
}

void bsp_sw_qei_reset(uint8_t index)
{
    if (index >= BSP_SW_QEI_MAX) {
        return;
    }

    IntMasterDisable();
    s_sw_qei[index].count = 0;
    s_sw_qei[index].last_step = 0;
    s_sw_qei[index].prev_state = sw_qei_read_state(&s_sw_qei[index]);
    IntMasterEnable();
}
