/**
 * @file bsp_sw_qei.c
 * @brief GPIO 边沿中断 + 轮询备份的软件正交解码（M3/M4 等无硬件 QEI 通道）
 */

#include "bsp_sw_qei.h"

#include <stddef.h>

#include "bsp_gpio.h"
#include "driverlib/gpio.h"
#include "driverlib/interrupt.h"
#include "inc/hw_ints.h"
#include "inc/hw_memmap.h"

#define BSP_SW_QEI_MAX        4U
/** poll 备份：每轮公平轮转最大步数 */
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

static bool sw_qei_decode_step(bsp_sw_qei_state_t *st)
{
    uint8_t state;
    uint8_t delta;
    uint8_t idx;
    int8_t step;
    bool progressed = false;

    IntMasterDisable();
    state = sw_qei_read_state(st);
    delta = (uint8_t)(st->prev_state ^ state);

    if (delta == 0U) {
        IntMasterEnable();
        return false;
    }

    if (delta == 3U) {
        if (st->last_step != 0) {
            st->count += st->last_step;
        }
        st->prev_state = state;
        IntMasterEnable();
        return true;
    }

    idx = (uint8_t)((st->prev_state << 2U) | state);
    step = s_qdec_lut[idx];
    st->prev_state = state;
    if (step != 0) {
        st->count += step;
        st->last_step = step;
        progressed = true;
    }

    IntMasterEnable();
    return progressed;
}

static void sw_qei_poll_channel(bsp_sw_qei_state_t *st)
{
    uint16_t n;

    for (n = 0U; n < BSP_SW_QEI_POLL_BURST; n++) {
        if (!sw_qei_decode_step(st)) {
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
            if (s_sw_qei[i].used && sw_qei_decode_step(&s_sw_qei[i])) {
                progressed = true;
            }
        }
        if (!progressed) {
            break;
        }
    }
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
            (void)sw_qei_decode_step(st);
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
    bsp_gpio_commit_locked_pins(pin->port_base, pin->pin_mask);
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
    st->last_step = 0;
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
