/* Auto-generated board module — edit .syscfg, not this file */
#include <stdbool.h>
#include <stdint.h>
#include "driverlib/gpio.h"
#include "driverlib/pin_map.h"
#include "driverlib/sysctl.h"
#include "inc/hw_memmap.h"
#include "driverlib/uart.h"
#include "driverlib/i2c.h"
#include "driverlib/adc.h"
#include "driverlib/ssi.h"
#include "driverlib/udma.h"
#include "board.h"

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

static void gpio_inputs(uint32_t port, uint8_t pins, bool pullup)
{
    GPIOPinTypeGPIOInput(port, pins);
    if (pullup) {
        GPIOPadConfigSet(port, pins, GPIO_STRENGTH_2MA, GPIO_PIN_TYPE_STD_WPU);
    }
}

void Board_Periph_Init(void) {
    gpio_enable_ports(0x37u);
    gpio_inputs(GPIO_PORTB_BASE, GPIO_PIN_4 | GPIO_PIN_5, true);
    gpio_inputs(GPIO_PORTC_BASE, GPIO_PIN_5, true);
    gpio_outputs(GPIO_PORTE_BASE, GPIO_PIN_3 | GPIO_PIN_4);
    gpio_inputs(GPIO_PORTE_BASE, GPIO_PIN_5, false);
    gpio_outputs(GPIO_PORTF_BASE, GPIO_PIN_4);
    GPIOPinConfigure(GPIO_PA0_U0RX);
    GPIOPinTypeUART(GPIO_PORTA_BASE, GPIO_PIN_0);
    GPIOPinConfigure(GPIO_PA1_U0TX);
    GPIOPinTypeUART(GPIO_PORTA_BASE, GPIO_PIN_1);
    GPIOPinConfigure(GPIO_PE1_U7TX);
    GPIOPinTypeUART(GPIO_PORTE_BASE, GPIO_PIN_1);
    GPIOPinConfigure(GPIO_PB2_I2C0SCL);
    GPIOPinTypeI2C(GPIO_PORTB_BASE, GPIO_PIN_2);
    GPIOPinConfigure(GPIO_PB3_I2C0SDA);
    GPIOPinTypeI2C(GPIO_PORTB_BASE, GPIO_PIN_3);
    GPIOPinTypeADC(GPIO_PORTE_BASE, GPIO_PIN_0);
    GPIOPinConfigure(GPIO_PF0_SSI1RX);
    GPIOPinConfigure(GPIO_PF1_SSI1TX);
    GPIOPinConfigure(GPIO_PF2_SSI1CLK);
    GPIOPinConfigure(GPIO_PF3_SSI1FSS);
    GPIOPinTypeSSI(GPIO_PORTF_BASE, GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3);
    SysCtlPeripheralEnable(SYSCTL_PERIPH_UART0);
    UARTConfigSetExpClk(UART0_BASE, SysCtlClockGet(), 115200, UART_CONFIG_WLEN_8|UART_CONFIG_STOP_ONE|UART_CONFIG_PAR_NONE);
    UARTFIFOEnable(UART0_BASE);
    UARTEnable(UART0_BASE);
    SysCtlPeripheralEnable(SYSCTL_PERIPH_UART7);
    UARTConfigSetExpClk(UART7_BASE, SysCtlClockGet(), 115200, UART_CONFIG_WLEN_8|UART_CONFIG_STOP_ONE|UART_CONFIG_PAR_NONE);
    UARTFIFOEnable(UART7_BASE);
    UARTEnable(UART7_BASE);
    SysCtlPeripheralEnable(SYSCTL_PERIPH_I2C0);
    I2CMasterInitExpClk(I2C0_BASE, SysCtlClockGet(), true);
    SysCtlPeripheralEnable(SYSCTL_PERIPH_ADC0);
    ADCSequenceConfigure(ADC0_BASE, 3, ADC_TRIGGER_PROCESSOR, 0);
    ADCSequenceStepConfigure(ADC0_BASE, 3, 0, ADC_CTL_CH0|ADC_CTL_IE|ADC_CTL_END);
    ADCSequenceEnable(ADC0_BASE, 3);
    ADCProcessorTrigger(ADC0_BASE, 3);
    SysCtlPeripheralEnable(SYSCTL_PERIPH_SSI1);
    SSIConfigSetExpClk(SSI1_BASE, SysCtlClockGet(), SSI_FRF_MOTO_MODE_0, SSI_MODE_MASTER, 1000000, 8);
    SSIEnable(SSI1_BASE);
    SysCtlPeripheralEnable(SYSCTL_PERIPH_UDMA);
}

void UART_Putc(char c) {
    UARTCharPut(UART0_BASE, c);
}
void UART_Puts(const char* s) {
    while (*s) { UARTCharPut(UART0_BASE, *s++); }
}
int UART_Getc(char *c) {
    if (UARTCharsAvail(UART0_BASE)) {
        *c = (char)UARTCharGetNonBlocking(UART0_BASE);
        return 1;
    }
    return 0;
}

void UART_Debug_Putc(char c) {
    UARTCharPut(UART7_BASE, c);
}
void UART_Debug_Puts(const char* s) {
    while (*s) { UARTCharPut(UART7_BASE, *s++); }
}
int UART_Debug_Getc(char *c) {
    if (UARTCharsAvail(UART7_BASE)) {
        *c = (char)UARTCharGetNonBlocking(UART7_BASE);
        return 1;
    }
    return 0;
}