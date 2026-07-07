/* Auto-generated motor module — edit .syscfg, not this file */
#include <stdbool.h>
#include <stdint.h>
#include "driverlib/gpio.h"
#include "driverlib/pin_map.h"
#include "driverlib/sysctl.h"
#include "inc/hw_memmap.h"
#include "driverlib/timer.h"
#include "motor.h"

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

static void gpio_outputs(uint32_t port, uint8_t pins)
{
    GPIOPinTypeGPIOOutput(port, pins);
}

void Motor_Init(void) {
    gpio_enable_ports(0x27u);
    gpio_outputs(GPIO_PORTA_BASE, GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_6 | GPIO_PIN_7);
    gpio_outputs(GPIO_PORTC_BASE, GPIO_PIN_4 | GPIO_PIN_7);
    gpio_outputs(GPIO_PORTF_BASE, GPIO_PIN_0 | GPIO_PIN_4);
    GPIOPinConfigure(GPIO_PB6_T0CCP0);
    GPIOPinConfigure(GPIO_PB7_T0CCP1);
    GPIOPinConfigure(GPIO_PB4_T1CCP0);
    GPIOPinConfigure(GPIO_PB5_T1CCP1);
    GPIOPinTypeTimer(GPIO_PORTB_BASE, GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7);
    SysCtlPeripheralEnable(SYSCTL_PERIPH_TIMER0);
    TimerConfigure(TIMER0_BASE, TIMER_CFG_SPLIT_PAIR|TIMER_CFG_A_PWM|TIMER_CFG_B_PWM);
    TimerLoadSet(TIMER0_BASE, TIMER_A, 8000);
    TimerMatchSet(TIMER0_BASE, TIMER_A, 0);
    TimerEnable(TIMER0_BASE, TIMER_A);
    TimerLoadSet(TIMER0_BASE, TIMER_B, 8000);
    TimerMatchSet(TIMER0_BASE, TIMER_B, 0);
    TimerEnable(TIMER0_BASE, TIMER_B);
    SysCtlPeripheralEnable(SYSCTL_PERIPH_TIMER1);
    TimerConfigure(TIMER1_BASE, TIMER_CFG_SPLIT_PAIR|TIMER_CFG_A_PWM|TIMER_CFG_B_PWM);
    TimerLoadSet(TIMER1_BASE, TIMER_A, 8000);
    TimerMatchSet(TIMER1_BASE, TIMER_A, 0);
    TimerEnable(TIMER1_BASE, TIMER_A);
    TimerLoadSet(TIMER1_BASE, TIMER_B, 8000);
    TimerMatchSet(TIMER1_BASE, TIMER_B, 0);
    TimerEnable(TIMER1_BASE, TIMER_B);
}