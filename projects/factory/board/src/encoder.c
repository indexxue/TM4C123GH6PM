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
    /* SDK 顺序：GPIO 时钟 → QEI 时钟 → pin mux → QEIConfigure → QEIEnable */
    SysCtlPeripheralEnable(SYSCTL_PERIPH_QEI0);
    SysCtlPeripheralEnable(SYSCTL_PERIPH_QEI1);
    GPIOPinConfigure(GPIO_PC5_PHA1);
    GPIOPinConfigure(GPIO_PC6_PHB1);
    bsp_gpio_commit_locked_pins(GPIO_PORTC_BASE, GPIO_PIN_5 | GPIO_PIN_6);
    GPIOPinTypeQEI(GPIO_PORTC_BASE, GPIO_PIN_5 | GPIO_PIN_6);
    GPIOPadConfigSet(GPIO_PORTC_BASE, GPIO_PIN_5 | GPIO_PIN_6, GPIO_STRENGTH_2MA, GPIO_PIN_TYPE_STD_WPU);
    GPIOPinConfigure(GPIO_PD6_PHA0);
    GPIOPinConfigure(GPIO_PD7_PHB0);
    bsp_gpio_commit_locked_pins(GPIO_PORTD_BASE, GPIO_PIN_6 | GPIO_PIN_7);
    GPIOPinTypeQEI(GPIO_PORTD_BASE, GPIO_PIN_6 | GPIO_PIN_7);
    GPIOPadConfigSet(GPIO_PORTD_BASE, GPIO_PIN_6 | GPIO_PIN_7, GPIO_STRENGTH_2MA, GPIO_PIN_TYPE_STD_WPU);
    (void)bsp_qei_init(&BOARD_QEI_CFG);
    gpio_inputs(GPIO_PORTB_BASE, GPIO_PIN_0, true);
    gpio_inputs(GPIO_PORTD_BASE, GPIO_PIN_4 | GPIO_PIN_5, true);
    gpio_inputs(GPIO_PORTF_BASE, GPIO_PIN_3, true);
    static const bsp_sw_qei_channel_t sw_enc_0 = {
        .pin_a = { GPIO_PORTF_BASE, GPIO_PIN_3 },
        .pin_b = { GPIO_PORTD_BASE, GPIO_PIN_4 },
    };
    (void)bsp_sw_qei_register(0, &sw_enc_0);
    static const bsp_sw_qei_channel_t sw_enc_1 = {
        .pin_a = { GPIO_PORTD_BASE, GPIO_PIN_5 },
        .pin_b = { GPIO_PORTB_BASE, GPIO_PIN_0 },
    };
    (void)bsp_sw_qei_register(1, &sw_enc_1);
    bsp_sw_qei_enable();
}

int32_t Encoder_GetCount(uint8_t index)
{
    switch (index) {
    case 0: return bsp_qei_get_position(QEI1_BASE);
    case 1: return bsp_qei_get_position(QEI0_BASE);
    case 2: return bsp_sw_qei_get_count(0);
    case 3: return bsp_sw_qei_get_count(1);
    default: return 0;
    }
}

void Encoder_ResetCount(uint8_t index)
{
    switch (index) {
    case 0: bsp_qei_reset(QEI1_BASE); break;
    case 1: bsp_qei_reset(QEI0_BASE); break;
    case 2: bsp_sw_qei_reset(0); break;
    case 3: bsp_sw_qei_reset(1); break;
    default: break;
    }
}