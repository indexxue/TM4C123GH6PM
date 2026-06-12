/* Auto-generated encoder module — edit .syscfg, not this file */
#include <stdbool.h>
#include <stdint.h>
#include "driverlib/gpio.h"
#include "driverlib/pin_map.h"
#include "driverlib/sysctl.h"
#include "inc/hw_memmap.h"
#include "driverlib/timer.h"
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
    GPIOPinConfigure(GPIO_PD2_WT3CCP0);
    GPIOPinConfigure(GPIO_PD3_WT3CCP1);
    GPIOPinConfigure(GPIO_PD4_WT4CCP0);
    GPIOPinConfigure(GPIO_PD5_WT4CCP1);
    GPIOPinConfigure(GPIO_PD6_WT5CCP0);
    GPIOPinConfigure(GPIO_PD7_WT5CCP1);
    GPIOPinConfigure(GPIO_PC0_T4CCP0);
    GPIOPinConfigure(GPIO_PC1_T4CCP1);
    GPIOPinTypeTimer(GPIO_PORTC_BASE, GPIO_PIN_0 | GPIO_PIN_1);
    GPIOPinTypeTimer(GPIO_PORTD_BASE, GPIO_PIN_2 | GPIO_PIN_3 | GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7);
    SysCtlPeripheralEnable(SYSCTL_PERIPH_WTIMER3);
    TimerConfigure(WTIMER3_BASE, TIMER_CFG_SPLIT_PAIR|TIMER_CFG_A_CAP_TIME_UP|TIMER_CFG_B_CAP_TIME_UP);
    TimerControlEvent(WTIMER3_BASE, TIMER_A, TIMER_EVENT_BOTH_EDGES);
    TimerControlEvent(WTIMER3_BASE, TIMER_B, TIMER_EVENT_BOTH_EDGES);
    TimerEnable(WTIMER3_BASE, TIMER_A);
    TimerEnable(WTIMER3_BASE, TIMER_B);
    SysCtlPeripheralEnable(SYSCTL_PERIPH_WTIMER4);
    TimerConfigure(WTIMER4_BASE, TIMER_CFG_SPLIT_PAIR|TIMER_CFG_A_CAP_TIME_UP|TIMER_CFG_B_CAP_TIME_UP);
    TimerControlEvent(WTIMER4_BASE, TIMER_A, TIMER_EVENT_BOTH_EDGES);
    TimerControlEvent(WTIMER4_BASE, TIMER_B, TIMER_EVENT_BOTH_EDGES);
    TimerEnable(WTIMER4_BASE, TIMER_A);
    TimerEnable(WTIMER4_BASE, TIMER_B);
    SysCtlPeripheralEnable(SYSCTL_PERIPH_WTIMER5);
    TimerConfigure(WTIMER5_BASE, TIMER_CFG_SPLIT_PAIR|TIMER_CFG_A_CAP_TIME_UP|TIMER_CFG_B_CAP_TIME_UP);
    TimerControlEvent(WTIMER5_BASE, TIMER_A, TIMER_EVENT_BOTH_EDGES);
    TimerControlEvent(WTIMER5_BASE, TIMER_B, TIMER_EVENT_BOTH_EDGES);
    TimerEnable(WTIMER5_BASE, TIMER_A);
    TimerEnable(WTIMER5_BASE, TIMER_B);
    SysCtlPeripheralEnable(SYSCTL_PERIPH_TIMER4);
    TimerConfigure(TIMER4_BASE, TIMER_CFG_SPLIT_PAIR|TIMER_CFG_A_CAP_TIME_UP|TIMER_CFG_B_CAP_TIME_UP);
    TimerControlEvent(TIMER4_BASE, TIMER_A, TIMER_EVENT_BOTH_EDGES);
    TimerControlEvent(TIMER4_BASE, TIMER_B, TIMER_EVENT_BOTH_EDGES);
    TimerEnable(TIMER4_BASE, TIMER_A);
    TimerEnable(TIMER4_BASE, TIMER_B);
}