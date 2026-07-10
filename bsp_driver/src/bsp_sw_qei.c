/**
 * @file bsp_sw_qei.c
 * @brief GPIO 边沿中断 + 4x 正交状态表软件编码器
 */

#include "bsp_sw_qei.h"

#include <stddef.h>

#include "driverlib/gpio.h"
#include "driverlib/interrupt.h"
#include "inc/hw_ints.h"
#include "inc/hw_memmap.h"

#define BSP_SW_QEI_MAX 4U

typedef struct {
    bsp_sw_qei_channel_t ch;
    volatile int32_t count;
    uint8_t prev_state;
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

static void sw_qei_update(bsp_sw_qei_state_t *st)
{
    uint8_t state;
    uint8_t idx;

    state = sw_qei_read_state(st);
    idx = (uint8_t)((st->prev_state << 2U) | state);
    st->count += s_qdec_lut[idx];
    st->prev_state = state;
}

static void sw_qei_port_handler(uint32_t port_base)
{
    uint32_t status;
    uint8_t i;

    status = GPIOIntStatus(port_base, true);
    for (i = 0U; i < BSP_SW_QEI_MAX; i++) {
        bsp_sw_qei_state_t *st = &s_sw_qei[i];
        uint8_t hit_mask;

        if (!st->used) {
            continue;
        }

        hit_mask = 0U;
        if (st->ch.pin_a.port_base == port_base) {
            hit_mask |= st->ch.pin_a.pin_mask;
        }
        if (st->ch.pin_b.port_base == port_base) {
            hit_mask |= st->ch.pin_b.pin_mask;
        }
        if ((status & hit_mask) != 0U) {
            sw_qei_update(st);
        }
    }
    GPIOIntClear(port_base, status);
}

static void sw_qei_portb_handler(void)
{
    sw_qei_port_handler(GPIO_PORTB_BASE);
}

static void sw_qei_portd_handler(void)
{
    sw_qei_port_handler(GPIO_PORTD_BASE);
}

static void sw_qei_portf_handler(void)
{
    sw_qei_port_handler(GPIO_PORTF_BASE);
}

static void sw_qei_arm_pin(const bsp_gpio_pin_t *pin)
{
    GPIOIntTypeSet(pin->port_base, pin->pin_mask, GPIO_BOTH_EDGES);
    GPIOIntEnable(pin->port_base, pin->pin_mask);
}

static void sw_qei_enable_port(uint32_t port_base, void (*handler)(void), uint32_t irqn)
{
    GPIOIntRegister(port_base, handler);
    IntPrioritySet(irqn, 0x80U);
    IntEnable(irqn);
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
    st->used = true;
    return true;
}

void bsp_sw_qei_enable(void)
{
    uint8_t i;
    bool port_b = false;
    bool port_d = false;
    bool port_f = false;

    for (i = 0U; i < BSP_SW_QEI_MAX; i++) {
        const bsp_sw_qei_state_t *st = &s_sw_qei[i];

        if (!st->used) {
            continue;
        }

        sw_qei_arm_pin(&st->ch.pin_a);
        sw_qei_arm_pin(&st->ch.pin_b);

        if (st->ch.pin_a.port_base == GPIO_PORTB_BASE || st->ch.pin_b.port_base == GPIO_PORTB_BASE) {
            port_b = true;
        }
        if (st->ch.pin_a.port_base == GPIO_PORTD_BASE || st->ch.pin_b.port_base == GPIO_PORTD_BASE) {
            port_d = true;
        }
        if (st->ch.pin_a.port_base == GPIO_PORTF_BASE || st->ch.pin_b.port_base == GPIO_PORTF_BASE) {
            port_f = true;
        }
    }

    if (port_b) {
        sw_qei_enable_port(GPIO_PORTB_BASE, sw_qei_portb_handler, INT_GPIOB);
    }
    if (port_d) {
        sw_qei_enable_port(GPIO_PORTD_BASE, sw_qei_portd_handler, INT_GPIOD);
    }
    if (port_f) {
        sw_qei_enable_port(GPIO_PORTF_BASE, sw_qei_portf_handler, INT_GPIOF);
    }
}

int32_t bsp_sw_qei_get_count(uint8_t index)
{
    if (index >= BSP_SW_QEI_MAX) {
        return 0;
    }
    return s_sw_qei[index].count;
}

void bsp_sw_qei_reset(uint8_t index)
{
    if (index >= BSP_SW_QEI_MAX) {
        return;
    }
    s_sw_qei[index].count = 0;
}
