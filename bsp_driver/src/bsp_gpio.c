/**
 * @file bsp_gpio.c
 * @brief TM4C123 GPIO
 */

#include "bsp_gpio.h"

#include "bsp_sysctl.h"

#include "bsp_config.h"

#include "driverlib/gpio.h"
#include "driverlib/sysctl.h"

#define BSP_GPIO_PERIPH_READY_US 100000U

static bool gpio_wait_port_ready(uint32_t port_mask)
{
    if (port_mask & (1u << 0)) {
        if (!bsp_periph_wait_ready(SYSCTL_PERIPH_GPIOA, BSP_GPIO_PERIPH_READY_US)) {
            return false;
        }
    }
    if (port_mask & (1u << 1)) {
        if (!bsp_periph_wait_ready(SYSCTL_PERIPH_GPIOB, BSP_GPIO_PERIPH_READY_US)) {
            return false;
        }
    }
    if (port_mask & (1u << 2)) {
        if (!bsp_periph_wait_ready(SYSCTL_PERIPH_GPIOC, BSP_GPIO_PERIPH_READY_US)) {
            return false;
        }
    }
    if (port_mask & (1u << 3)) {
        if (!bsp_periph_wait_ready(SYSCTL_PERIPH_GPIOD, BSP_GPIO_PERIPH_READY_US)) {
            return false;
        }
    }
    if (port_mask & (1u << 4)) {
        if (!bsp_periph_wait_ready(SYSCTL_PERIPH_GPIOE, BSP_GPIO_PERIPH_READY_US)) {
            return false;
        }
    }
    if (port_mask & (1u << 5)) {
        if (!bsp_periph_wait_ready(SYSCTL_PERIPH_GPIOF, BSP_GPIO_PERIPH_READY_US)) {
            return false;
        }
    }

    return true;
}

bool bsp_gpio_port_enable(uint32_t port_mask)
{
    if (port_mask & (1u << 0)) {
        SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOA);
    }
    if (port_mask & (1u << 1)) {
        SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOB);
    }
    if (port_mask & (1u << 2)) {
        SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOC);
    }
    if (port_mask & (1u << 3)) {
        SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOD);
    }
    if (port_mask & (1u << 4)) {
        SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOE);
    }
    if (port_mask & (1u << 5)) {
        SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOF);
    }

    return gpio_wait_port_ready(port_mask);
}

void bsp_gpio_configure(const bsp_gpio_pin_t *pin, bsp_gpio_dir_t dir, bsp_gpio_pull_t pull)
{
    if (pin == NULL) {
        return;
    }

    if (dir == BSP_GPIO_DIR_OUTPUT) {
        GPIOPinTypeGPIOOutput(pin->port_base, pin->pin_mask);
        return;
    }

    GPIOPinTypeGPIOInput(pin->port_base, pin->pin_mask);
    if (pull == BSP_GPIO_PULL_UP) {
        GPIOPadConfigSet(pin->port_base, pin->pin_mask, GPIO_STRENGTH_2MA, GPIO_PIN_TYPE_STD_WPU);
    }
}

void bsp_gpio_write(const bsp_gpio_pin_t *pin, bool high)
{
    if (pin == NULL) {
        return;
    }

    if (high) {
        GPIOPinWrite(pin->port_base, pin->pin_mask, pin->pin_mask);
    } else {
        GPIOPinWrite(pin->port_base, pin->pin_mask, 0U);
    }
}

bool bsp_gpio_read(const bsp_gpio_pin_t *pin)
{
    if (pin == NULL) {
        return false;
    }

    return GPIOPinRead(pin->port_base, pin->pin_mask) != 0U;
}

void bsp_gpio_write_port(uint32_t port_base, uint8_t pin_mask, uint8_t value_mask)
{
    GPIOPinWrite(port_base, pin_mask, value_mask & pin_mask);
}
