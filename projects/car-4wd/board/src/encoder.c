/* Auto-generated encoder module — edit .syscfg, not this file */
#include <stdbool.h>
#include <stdint.h>
#include "bsp_gpio.h"
#include "bsp_qei.h"
#include "driverlib/gpio.h"
#include "driverlib/pin_map.h"
#include "driverlib/sysctl.h"
#include "inc/hw_memmap.h"
#include "board.h"



void Encoder_Init(void) {
    (void)bsp_gpio_port_enable(0x0Cu);
    GPIOPinConfigure(GPIO_PC5_PHA1);
    GPIOPinConfigure(GPIO_PC6_PHB1);
    GPIOPinTypeQEI(GPIO_PORTC_BASE, GPIO_PIN_5 | GPIO_PIN_6);
    GPIOPinConfigure(GPIO_PD6_PHA0);
    GPIOPinConfigure(GPIO_PD7_PHB0);
    GPIOPinTypeQEI(GPIO_PORTD_BASE, GPIO_PIN_6 | GPIO_PIN_7);
    bsp_qei_init(&BOARD_QEI_CFG);
}

int32_t Encoder_GetCount(uint8_t index)
{
    switch (index) {
    case 0: return bsp_qei_get_position(QEI1_BASE);
    case 1: return bsp_qei_get_position(QEI0_BASE);
    default: return 0;
    }
}