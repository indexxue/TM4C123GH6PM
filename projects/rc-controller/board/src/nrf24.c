/**
 * @file    nrf24.c
 * @brief   NRF24 CE/CS/IRQ 引脚默认态（SPI 协议驱动待接）
 */

#include "nrf24.h"

#include "board.h"

#include "driverlib/gpio.h"

void board_nrf24_gpio_init(void)
{
    GPIOPinTypeGPIOOutput(GPIO_NRF_CS_PORT, GPIO_NRF_CS_MASK);
    GPIOPinWrite(GPIO_NRF_CS_PORT, GPIO_NRF_CS_MASK, GPIO_NRF_CS_MASK);

    GPIOPinTypeGPIOOutput(GPIO_NRF_CE_PORT, GPIO_NRF_CE_MASK);
    GPIOPinWrite(GPIO_NRF_CE_PORT, GPIO_NRF_CE_MASK, 0U);
}
