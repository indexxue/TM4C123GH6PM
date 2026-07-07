/**
 * @file bsp_gpio.h
 * @brief TM4C123 GPIO 薄封装
 */

#ifndef BSP_DRIVER_GPIO_H
#define BSP_DRIVER_GPIO_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t port_base;
    uint8_t pin_mask;
} bsp_gpio_pin_t;

typedef enum {
    BSP_GPIO_DIR_INPUT = 0,
    BSP_GPIO_DIR_OUTPUT,
} bsp_gpio_dir_t;

typedef enum {
    BSP_GPIO_PULL_NONE = 0,
    BSP_GPIO_PULL_UP,
} bsp_gpio_pull_t;

bool bsp_gpio_port_enable(uint32_t port_mask);
void bsp_gpio_configure(const bsp_gpio_pin_t *pin, bsp_gpio_dir_t dir, bsp_gpio_pull_t pull);
void bsp_gpio_write(const bsp_gpio_pin_t *pin, bool high);
bool bsp_gpio_read(const bsp_gpio_pin_t *pin);
void bsp_gpio_write_port(uint32_t port_base, uint8_t pin_mask, uint8_t value_mask);

#endif /* BSP_DRIVER_GPIO_H */
