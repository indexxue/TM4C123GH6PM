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
#include "inc/hw_memmap.h"
#include "board.h"

static void gpio_outputs(uint32_t port, uint8_t pins)
{
    GPIOPinTypeGPIOOutput(port, pins);
}

static void gpio_inputs(uint32_t port, uint8_t pins, bool pullup)
{
    GPIOPinTypeGPIOInput(port, pins);
    if (pullup) {
        GPIOPadConfigSet(port, pins, GPIO_STRENGTH_2MA, GPIO_PIN_TYPE_STD_WPU);
    }
}

static const bsp_pwm_channel_t board_pwm_channels[] = {
    { TIMER0_BASE, SYSCTL_PERIPH_TIMER0, TIMER_A, 10000 },
    { TIMER0_BASE, SYSCTL_PERIPH_TIMER0, TIMER_B, 10000 },
    { TIMER1_BASE, SYSCTL_PERIPH_TIMER1, TIMER_A, 10000 },
    { TIMER1_BASE, SYSCTL_PERIPH_TIMER1, TIMER_B, 10000 },
};
const bsp_pwm_config_t BOARD_PWM_CFG = {
    .channels = board_pwm_channels,
    .channel_count = 4,
    .clock_hz = 80000000,
};

static const bsp_qei_channel_t board_qei_channels[] = {
    { QEI1_BASE, SYSCTL_PERIPH_QEI1 },
    { QEI0_BASE, SYSCTL_PERIPH_QEI0 },
};
const bsp_qei_config_t BOARD_QEI_CFG = {
    .channels = board_qei_channels,
    .channel_count = 2,
};

const bsp_uart_config_t BOARD_UART_BT_CFG = { UART0_BASE, 115200 };

const bsp_uart_config_t BOARD_UART_DEBUG_CFG = { UART7_BASE, 115200 };

const bsp_i2c_config_t BOARD_I2C_CFG = { I2C0_BASE, 400000 };

static const bsp_adc_channel_t board_adc_channels[] = {
    { 0, 0 },
    { 1, 1 },
    { 4, 2 },
    { 5, 3 },
    { 6, 4 },
    { 7, 5 },
    { 8, 6 },
    { 9, 7 },
};
const bsp_adc_config_t BOARD_ADC_CFG = {
    .base = ADC1_BASE,
    .sequence = 3,
    .channels = board_adc_channels,
    .channel_count = 8,
};

static const bsp_adc_channel_t board_battery_channel = { 0, 0 };
const bsp_adc_config_t BOARD_BATTERY_ADC_CFG = {
    .base = ADC1_BASE,
    .sequence = 2,
    .channels = &board_battery_channel,
    .channel_count = 1,
};

const bsp_spi_config_t BOARD_SPI_CFG = {
    .base = SSI0_BASE,
    .clock_hz = 1000000,
    .mode = BSP_SPI_MODE_0,
    .data_bits = 8,
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
    if (!bsp_gpio_port_enable(0x1Fu)) {
        return false;
    }
    gpio_outputs(GPIO_PORTB_BASE, GPIO_PIN_1);
    gpio_outputs(GPIO_PORTC_BASE, GPIO_PIN_2 | GPIO_PIN_3);
    gpio_inputs(GPIO_PORTC_BASE, GPIO_PIN_1, false);
    GPIOPinConfigure(GPIO_PA0_U0RX);
    GPIOPinTypeUART(GPIO_PORTA_BASE, GPIO_PIN_0);
    GPIOPinConfigure(GPIO_PA1_U0TX);
    GPIOPinTypeUART(GPIO_PORTA_BASE, GPIO_PIN_1);
    GPIOPinConfigure(GPIO_PE0_U7RX);
    GPIOPinTypeUART(GPIO_PORTE_BASE, GPIO_PIN_0);
    GPIOPinConfigure(GPIO_PE1_U7TX);
    GPIOPinTypeUART(GPIO_PORTE_BASE, GPIO_PIN_1);
    GPIOPinConfigure(GPIO_PB2_I2C0SCL);
    GPIOPinTypeI2C(GPIO_PORTB_BASE, GPIO_PIN_2);
    GPIOPinConfigure(GPIO_PB3_I2C0SDA);
    GPIOPinTypeI2C(GPIO_PORTB_BASE, GPIO_PIN_3);
    GPIOPinTypeADC(GPIO_PORTE_BASE, GPIO_PIN_3);
    GPIOPinTypeADC(GPIO_PORTE_BASE, GPIO_PIN_2);
    GPIOPinTypeADC(GPIO_PORTD_BASE, GPIO_PIN_3);
    GPIOPinTypeADC(GPIO_PORTD_BASE, GPIO_PIN_2);
    GPIOPinTypeADC(GPIO_PORTD_BASE, GPIO_PIN_1);
    GPIOPinTypeADC(GPIO_PORTD_BASE, GPIO_PIN_0);
    GPIOPinTypeADC(GPIO_PORTE_BASE, GPIO_PIN_5);
    GPIOPinTypeADC(GPIO_PORTE_BASE, GPIO_PIN_4);
    GPIOPinConfigure(GPIO_PA4_SSI0RX);
    GPIOPinConfigure(GPIO_PA5_SSI0TX);
    GPIOPinConfigure(GPIO_PA2_SSI0CLK);
    GPIOPinConfigure(GPIO_PA3_SSI0FSS);
    GPIOPinTypeSSI(GPIO_PORTA_BASE, GPIO_PIN_2 | GPIO_PIN_3 | GPIO_PIN_4 | GPIO_PIN_5);
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
    if (!bsp_adc_init(&BOARD_ADC_CFG)) {
        return false;
    }
    if (!bsp_adc_init(&BOARD_BATTERY_ADC_CFG)) {
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

void UART_Debug_Putc(char c) {
    bsp_uart_putc(UART7_BASE, c);
}
void UART_Debug_Puts(const char* s) {
    bsp_uart_puts(UART7_BASE, s);
}
int UART_Debug_Getc(char *c) {
    return bsp_uart_getc(UART7_BASE, c);
}