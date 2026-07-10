/* Auto-generated encoder module — edit .syscfg, not this file */
#include <stdbool.h>
#include <stdint.h>
#include "bsp_gpio.h"
#include "bsp_qei.h"
#include "driverlib/gpio.h"
#include "driverlib/pin_map.h"
#include "driverlib/sysctl.h"
#include "inc/hw_memmap.h"
#include "bsp_sw_qei.h"
#include "board.h"


static void gpio_inputs(uint32_t port, uint8_t pins, bool pullup)
{
    bsp_gpio_commit_locked_pins(port, pins);
    GPIOPinTypeGPIOInput(port, pins);
    if (pullup) {
        GPIOPadConfigSet(port, pins, GPIO_STRENGTH_2MA, GPIO_PIN_TYPE_STD_WPU);
    }
}

void Encoder_Init(void) {
    (void)bsp_gpio_port_enable(0x2Eu);
    gpio_inputs(GPIO_PORTB_BASE, GPIO_PIN_0, true);
    gpio_inputs(GPIO_PORTC_BASE, GPIO_PIN_5 | GPIO_PIN_6, true);
    gpio_inputs(GPIO_PORTD_BASE, GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7, true);
    gpio_inputs(GPIO_PORTF_BASE, GPIO_PIN_3, true);
    static const bsp_sw_qei_channel_t sw_enc_0 = {
        .pin_a = { GPIO_PORTC_BASE, GPIO_PIN_5 },
        .pin_b = { GPIO_PORTC_BASE, GPIO_PIN_6 },
    };
    (void)bsp_sw_qei_register(0, &sw_enc_0);
    static const bsp_sw_qei_channel_t sw_enc_1 = {
        .pin_a = { GPIO_PORTD_BASE, GPIO_PIN_6 },
        .pin_b = { GPIO_PORTD_BASE, GPIO_PIN_7 },
    };
    (void)bsp_sw_qei_register(1, &sw_enc_1);
    static const bsp_sw_qei_channel_t sw_enc_2 = {
        .pin_a = { GPIO_PORTF_BASE, GPIO_PIN_3 },
        .pin_b = { GPIO_PORTD_BASE, GPIO_PIN_4 },
    };
    (void)bsp_sw_qei_register(2, &sw_enc_2);
    static const bsp_sw_qei_channel_t sw_enc_3 = {
        .pin_a = { GPIO_PORTD_BASE, GPIO_PIN_5 },
        .pin_b = { GPIO_PORTB_BASE, GPIO_PIN_0 },
    };
    (void)bsp_sw_qei_register(3, &sw_enc_3);
    bsp_sw_qei_enable();
}

int32_t Encoder_GetCount(uint8_t index)
{
    switch (index) {
    case 0: return bsp_sw_qei_get_count(0);
    case 1: return bsp_sw_qei_get_count(1);
    case 2: return bsp_sw_qei_get_count(2);
    case 3: return bsp_sw_qei_get_count(3);
    default: return 0;
    }
}