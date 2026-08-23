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
    GPIOPinWrite(port, pins, 0);
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
    gpio_outputs(GPIO_PORTA_BASE, GPIO_PIN_6);
    gpio_outputs(GPIO_PORTC_BASE, GPIO_PIN_4 | GPIO_PIN_7);
    gpio_outputs(GPIO_PORTF_BASE, GPIO_PIN_4);
    GPIOPinConfigure(GPIO_PB6_T0CCP0);
    GPIOPinConfigure(GPIO_PB7_T0CCP1);
    GPIOPinTypeTimer(GPIO_PORTB_BASE, GPIO_PIN_6 | GPIO_PIN_7);
    bsp_pwm_init(&BOARD_PWM_CFG);
}
void Motor_SetOutput(uint8_t motor_id, int32_t rpm, uint16_t duty_permille)
{
    if (duty_permille > 1000U) {
        duty_permille = 1000U;
    }
    switch (motor_id) {
    case 1:
        motor_apply_dir(
            &(const bsp_gpio_pin_t){GPIO_M1_IN1_PORT, GPIO_M1_IN1_MASK},
            &(const bsp_gpio_pin_t){GPIO_M1_IN2_PORT, GPIO_M1_IN2_MASK}, rpm);
        bsp_pwm_set_duty(M1_TIMER, M1_PWM_CH, duty_permille);
        break;
    case 2:
        motor_apply_dir(
            &(const bsp_gpio_pin_t){GPIO_M2_IN1_PORT, GPIO_M2_IN1_MASK},
            &(const bsp_gpio_pin_t){GPIO_M2_IN2_PORT, GPIO_M2_IN2_MASK}, rpm);
        bsp_pwm_set_duty(M2_TIMER, M2_PWM_CH, duty_permille);
        break;
    default:
        break;
    }
}

static uint16_t motor_rpm_to_duty(int32_t rpm)
{
    int32_t abs_rpm;
    uint16_t duty;
    if (rpm == 0) {
        return 0U;
    }
    abs_rpm = rpm;
    if (abs_rpm < 0) {
        abs_rpm = -abs_rpm;
    }
    if (abs_rpm > MOTOR_RPM_FULL_SCALE) {
        abs_rpm = MOTOR_RPM_FULL_SCALE;
    }
    duty = (uint16_t)((abs_rpm * 1000) / MOTOR_RPM_FULL_SCALE);
    if (duty < MOTOR_MIN_DUTY && abs_rpm > 0) {
        duty = MOTOR_MIN_DUTY;
    }
    return duty;
}

void Motor_SetSpeed(uint8_t motor_id, int32_t rpm)
{
    Motor_SetOutput(motor_id, rpm, motor_rpm_to_duty(rpm));
}