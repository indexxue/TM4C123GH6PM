/* Auto-generated motor module — edit .syscfg, not this file */
#include <stdbool.h>
#include <stdint.h>
#include "bsp_gpio.h"
#include "bsp_timer.h"
#include "driverlib/gpio.h"
#include "driverlib/pin_map.h"
#include "driverlib/sysctl.h"
#include "inc/hw_memmap.h"
#include "driverlib/timer.h"
#include "board.h"

static void gpio_outputs(uint32_t port, uint8_t pins)
{
    bsp_gpio_commit_locked_pins(port, pins);
    GPIOPinTypeGPIOOutput(port, pins);
}

static void motor_apply_dir(const bsp_gpio_pin_t *in1, const bsp_gpio_pin_t *in2, int32_t rpm)
{
    if (rpm > 0) {
        bsp_gpio_write(in1, true);
        bsp_gpio_write(in2, false);
    } else if (rpm < 0) {
        bsp_gpio_write(in1, false);
        bsp_gpio_write(in2, true);
    } else {
        bsp_gpio_write(in1, false);
        bsp_gpio_write(in2, false);
    }
}

void Motor_Init(void) {
    (void)bsp_gpio_port_enable(0x27u);
    gpio_outputs(GPIO_PORTA_BASE, GPIO_PIN_0);
    gpio_outputs(GPIO_PORTC_BASE, GPIO_PIN_4 | GPIO_PIN_7);
    gpio_outputs(GPIO_PORTF_BASE, GPIO_PIN_4);
    GPIOPinConfigure(GPIO_PB6_T0CCP0);
    GPIOPinConfigure(GPIO_PB7_T0CCP1);
    GPIOPinTypeTimer(GPIO_PORTB_BASE, GPIO_PIN_6 | GPIO_PIN_7);
    bsp_pwm_init(&BOARD_PWM_CFG);
}

void Motor_SetSpeed(uint8_t motor_id, int32_t rpm)
{
    uint16_t duty = (rpm == 0) ? 0U : 500U;
    switch (motor_id) {
    case 1:
        motor_apply_dir(
            &(const bsp_gpio_pin_t){GPIO_M1_IN1_PORT, GPIO_M1_IN1_MASK},
            &(const bsp_gpio_pin_t){GPIO_M1_IN2_PORT, GPIO_M1_IN2_MASK}, rpm);
        bsp_pwm_set_duty(M1_TIMER, M1_PWM_CH, duty);
        break;
    case 2:
        motor_apply_dir(
            &(const bsp_gpio_pin_t){GPIO_M2_IN1_PORT, GPIO_M2_IN1_MASK},
            &(const bsp_gpio_pin_t){GPIO_M2_IN2_PORT, GPIO_M2_IN2_MASK}, rpm);
        bsp_pwm_set_duty(M2_TIMER, M2_PWM_CH, duty);
        break;
    default:
        break;
    }
}