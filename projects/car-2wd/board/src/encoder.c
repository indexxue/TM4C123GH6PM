/* Auto-generated encoder module — edit .syscfg, not this file */
#include <stdbool.h>
#include <stdint.h>
#include "driverlib/gpio.h"
#include "driverlib/pin_map.h"
#include "driverlib/sysctl.h"
#include "inc/hw_memmap.h"
#include "driverlib/timer.h"
#include "driverlib/qei.h"
#include "encoder.h"

static void gpio_enable_ports(uint32_t ports)
{
    if (ports & (1u << 0)) { SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOA); }
    if (ports & (1u << 1)) { SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOB); }
    if (ports & (1u << 2)) { SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOC); }
    if (ports & (1u << 3)) { SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOD); }
    if (ports & (1u << 4)) { SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOE); }
    if (ports & (1u << 5)) { SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOF); }
    while (!SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOA)) {}
}

void Encoder_Init(void) {
    gpio_enable_ports(0x0Cu);
    GPIOPinConfigure(GPIO_PC5_PHA1);
    GPIOPinConfigure(GPIO_PC6_PHB1);
    GPIOPinTypeQEI(GPIO_PORTC_BASE, GPIO_PIN_5 | GPIO_PIN_6);
    GPIOPinConfigure(GPIO_PD6_PHA0);
    GPIOPinConfigure(GPIO_PD7_PHB0);
    GPIOPinTypeQEI(GPIO_PORTD_BASE, GPIO_PIN_6 | GPIO_PIN_7);
    SysCtlPeripheralEnable(SYSCTL_PERIPH_QEI1);
    QEIConfigure(QEI1_BASE, QEI_CONFIG_CAPTURE_A_B|QEI_CONFIG_NO_RESET|QEI_CONFIG_QUADRATURE|QEI_CONFIG_NO_SWAP, 0);
    QEIEnable(QEI1_BASE);
    SysCtlPeripheralEnable(SYSCTL_PERIPH_QEI0);
    QEIConfigure(QEI0_BASE, QEI_CONFIG_CAPTURE_A_B|QEI_CONFIG_NO_RESET|QEI_CONFIG_QUADRATURE|QEI_CONFIG_NO_SWAP, 0);
    QEIEnable(QEI0_BASE);
}