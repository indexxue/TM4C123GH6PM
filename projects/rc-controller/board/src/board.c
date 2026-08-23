/* Auto-generated board module — edit .syscfg, not this file */
#include <stdbool.h>
#include <stdint.h>
#include "bsp_gpio.h"
#include "bsp_uart.h"
#include "bsp_i2c.h"
#include "bsp_adc.h"
#include "bsp_dma.h"
#include "bsp_spi.h"
#include "driverlib/gpio.h"
#include "driverlib/pin_map.h"
#include "driverlib/sysctl.h"
#include "driverlib/timer.h"
#include "driverlib/uart.h"
#include "inc/hw_memmap.h"
#include "board.h"

static void gpio_outputs(uint32_t port, uint8_t pins)
{
    bsp_gpio_commit_locked_pins(port, pins);
    GPIOPinTypeGPIOOutput(port, pins);
    GPIOPinWrite(port, pins, 0);
}

static void gpio_inputs(uint32_t port, uint8_t pins, bool pullup)
{
    bsp_gpio_commit_locked_pins(port, pins);
    GPIOPinTypeGPIOInput(port, pins);
    if (pullup) {
        GPIOPadConfigSet(port, pins, GPIO_STRENGTH_2MA, GPIO_PIN_TYPE_STD_WPU);
    }
}

const bsp_uart_config_t BOARD_UART_BT_CFG = { UART0_BASE, 115200 };

const bsp_uart_config_t BOARD_UART_DEBUG_CFG = { UART7_BASE, 115200 };

const bsp_i2c_config_t BOARD_I2C_CFG = { I2C0_BASE, 400000 };

static const bsp_adc_channel_t board_battery_channel = { 0, 0 };
const bsp_adc_config_t BOARD_BATTERY_ADC_CFG = {
    .base = ADC1_BASE,
    .sequence = 2,
    .channels = &board_battery_channel,
    .channel_count = 1,
};

static const bsp_adc_channel_t board_joy_adc_channels[] = {
    { 7, 0 },
    { 6, 1 },
    { 5, 2 },
    { 4, 3 },
};
const bsp_adc_config_t BOARD_JOY_ADC_CFG = {
    .base = ADC0_BASE,
    .sequence = 0,
    .channels = board_joy_adc_channels,
    .channel_count = 4,
};

const bsp_spi_config_t BOARD_SPI_CFG = {
    .base = SSI0_BASE,
    .clock_hz = 10000000,
    .mode = BSP_SPI_MODE_0,
    .data_bits = 8,
    .role = BSP_SPI_ROLE_MASTER,
};

bool Board_UartDebug_Init(void) {
    if (!bsp_gpio_port_enable(0x10u)) {
        return false;
    }
    GPIOPinConfigure(GPIO_PE0_U7RX);
    GPIOPinTypeUART(GPIO_PORTE_BASE, GPIO_PIN_0);
    GPIOPinConfigure(GPIO_PE1_U7TX);
    GPIOPinTypeUART(GPIO_PORTE_BASE, GPIO_PIN_1);
    if (!bsp_uart_init(&BOARD_UART_DEBUG_CFG)) {
        return false;
    }
    return true;
}

bool Board_Periph_Init(void) {
    if (!bsp_gpio_port_enable(0x3Fu)) {
        return false;
    }
    gpio_outputs(GPIO_PORTA_BASE, GPIO_PIN_3 | GPIO_PIN_5 | GPIO_PIN_6);
    gpio_inputs(GPIO_PORTA_BASE, GPIO_PIN_2 | GPIO_PIN_4, false);
    gpio_outputs(GPIO_PORTB_BASE, GPIO_PIN_0);
    gpio_inputs(GPIO_PORTB_BASE, GPIO_PIN_1 | GPIO_PIN_6, true);
    gpio_outputs(GPIO_PORTC_BASE, GPIO_PIN_3);
    gpio_inputs(GPIO_PORTC_BASE, GPIO_PIN_4, true);
    gpio_outputs(GPIO_PORTF_BASE, GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2);
    GPIOPinConfigure(GPIO_PA0_U0RX);
    GPIOPinTypeUART(GPIO_PORTA_BASE, GPIO_PIN_0);
    GPIOPinConfigure(GPIO_PA1_U0TX);
    GPIOPinTypeUART(GPIO_PORTA_BASE, GPIO_PIN_1);
    GPIOPinConfigure(GPIO_PE0_U7RX);
    GPIOPinTypeUART(GPIO_PORTE_BASE, GPIO_PIN_0);
    GPIOPinConfigure(GPIO_PE1_U7TX);
    GPIOPinTypeUART(GPIO_PORTE_BASE, GPIO_PIN_1);
    GPIOPinTypeADC(GPIO_PORTD_BASE, GPIO_PIN_0);
    GPIOPinTypeADC(GPIO_PORTD_BASE, GPIO_PIN_1);
    GPIOPinTypeADC(GPIO_PORTD_BASE, GPIO_PIN_2);
    GPIOPinTypeADC(GPIO_PORTD_BASE, GPIO_PIN_3);
    GPIOPinTypeADC(GPIO_PORTE_BASE, GPIO_PIN_3);
    GPIOPinConfigure(GPIO_PA4_SSI0RX);
    GPIOPinConfigure(GPIO_PA5_SSI0TX);
    GPIOPinConfigure(GPIO_PA2_SSI0CLK);
    GPIOPinTypeSSI(GPIO_PORTA_BASE, GPIO_PIN_2 | GPIO_PIN_4 | GPIO_PIN_5);
    if (!bsp_dma_init()) {
        return false;
    }
    if (!bsp_uart_init(&BOARD_UART_BT_CFG)) {
        return false;
    }
    if (!bsp_uart_init(&BOARD_UART_DEBUG_CFG)) {
        return false;
    }
    if (!bsp_i2c_init(&BOARD_I2C_CFG)) {
        return false;
    }
    if (!bsp_adc_init(&BOARD_BATTERY_ADC_CFG)) {
        return false;
    }
    if (!bsp_adc_init(&BOARD_JOY_ADC_CFG)) {
        return false;
    }
    if (!bsp_spi_init(&BOARD_SPI_CFG)) {
        return false;
    }
    return true;
}

void UART_Putc(char c) {
    bsp_uart_putc(UART0_BASE, c);
}
void UART_Puts(const char* s) {
    bsp_uart_puts(UART0_BASE, s);
}
int UART_Getc(char *c) {
    return bsp_uart_getc(UART0_BASE, c);
}
void UART_Flush(void) {
    while (UARTBusy(UART0_BASE)) {
    }
}

void UART_Debug_Putc(char c) {
    bsp_uart_putc(UART7_BASE, c);
}
void UART_Debug_Puts(const char* s) {
    bsp_uart_puts(UART7_BASE, s);
}
int UART_Debug_Getc(char *c) {
    return bsp_uart_getc(UART7_BASE, c);
}