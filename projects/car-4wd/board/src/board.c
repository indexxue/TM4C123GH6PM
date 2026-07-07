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
    gpio_enable_ports(0x1Fu);
    gpio_outputs(GPIO_PORTC_BASE, GPIO_PIN_2 | GPIO_PIN_3);
    gpio_inputs(GPIO_PORTC_BASE, GPIO_PIN_1, false);
    gpio_outputs(GPIO_PORTD_BASE, GPIO_PIN_5);
    GPIOPinConfigure(GPIO_PB0_U1RX);
    GPIOPinTypeUART(GPIO_PORTB_BASE, GPIO_PIN_0);
    GPIOPinConfigure(GPIO_PB1_U1TX);
    GPIOPinTypeUART(GPIO_PORTB_BASE, GPIO_PIN_1);
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
    SysCtlPeripheralEnable(SYSCTL_PERIPH_UART1);
    UARTConfigSetExpClk(UART1_BASE, SysCtlClockGet(), 115200, UART_CONFIG_WLEN_8|UART_CONFIG_STOP_ONE|UART_CONFIG_PAR_NONE);
    UARTFIFOEnable(UART1_BASE);
    UARTEnable(UART1_BASE);
    SysCtlPeripheralEnable(SYSCTL_PERIPH_UART7);
    UARTConfigSetExpClk(UART7_BASE, SysCtlClockGet(), 115200, UART_CONFIG_WLEN_8|UART_CONFIG_STOP_ONE|UART_CONFIG_PAR_NONE);
    UARTFIFOEnable(UART7_BASE);
    UARTEnable(UART7_BASE);
    SysCtlPeripheralEnable(SYSCTL_PERIPH_I2C0);
    I2CMasterInitExpClk(I2C0_BASE, SysCtlClockGet(), true);
    SysCtlPeripheralEnable(SYSCTL_PERIPH_ADC1);
    ADCSequenceConfigure(ADC1_BASE, 3, ADC_TRIGGER_PROCESSOR, 0);
    ADCSequenceStepConfigure(ADC1_BASE, 3, 0, ADC_CTL_CH0);
    ADCSequenceStepConfigure(ADC1_BASE, 3, 1, ADC_CTL_CH1);
    ADCSequenceStepConfigure(ADC1_BASE, 3, 2, ADC_CTL_CH4);
    ADCSequenceStepConfigure(ADC1_BASE, 3, 3, ADC_CTL_CH5);
    ADCSequenceStepConfigure(ADC1_BASE, 3, 4, ADC_CTL_CH6);
    ADCSequenceStepConfigure(ADC1_BASE, 3, 5, ADC_CTL_CH7);
    ADCSequenceStepConfigure(ADC1_BASE, 3, 6, ADC_CTL_CH8);
    ADCSequenceStepConfigure(ADC1_BASE, 3, 7, ADC_CTL_CH9|ADC_CTL_END);
    ADCSequenceEnable(ADC1_BASE, 3);
    ADCProcessorTrigger(ADC1_BASE, 3);
    SysCtlPeripheralEnable(SYSCTL_PERIPH_SSI0);
    SSIConfigSetExpClk(SSI0_BASE, SysCtlClockGet(), SSI_FRF_MOTO_MODE_0, SSI_MODE_MASTER, 1000000, 8);
    SSIEnable(SSI0_BASE);
    SysCtlPeripheralEnable(SYSCTL_PERIPH_UDMA);
}

void UART_Putc(char c) {
    UARTCharPut(UART1_BASE, c);
}
void UART_Puts(const char* s) {
    while (*s) { UARTCharPut(UART1_BASE, *s++); }
}
int UART_Getc(char *c) {
    if (UARTCharsAvail(UART1_BASE)) {
        *c = (char)UARTCharGetNonBlocking(UART1_BASE);
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