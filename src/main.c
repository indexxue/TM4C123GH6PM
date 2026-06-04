#include <stdint.h>
#include <stdbool.h>
#include "inc/hw_memmap.h"
#include "inc/hw_gpio.h"
#include "inc/hw_types.h"
#include "driverlib/sysctl.h"
#include "driverlib/gpio.h"

static void delay(volatile uint32_t count) {
    while (count--) { __asm volatile ("nop"); }
}

int main(void) {
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOF);
    while (!SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOF)) { }

    HWREG(GPIO_PORTF_BASE + GPIO_O_LOCK) = GPIO_LOCK_KEY;
    HWREG(GPIO_PORTF_BASE + GPIO_O_CR) |=
        GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3 | GPIO_PIN_4;

    GPIOPinTypeGPIOOutput(GPIO_PORTF_BASE, GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3);
    HWREG(GPIO_PORTF_BASE + GPIO_O_DIR) &= ~(GPIO_PIN_0 | GPIO_PIN_4);
    HWREG(GPIO_PORTF_BASE + GPIO_O_DEN) |=  GPIO_PIN_0 | GPIO_PIN_4;
    HWREG(GPIO_PORTF_BASE + GPIO_O_PUR) |=  GPIO_PIN_0 | GPIO_PIN_4;

    GPIOPinWrite(GPIO_PORTF_BASE, GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3, GPIO_PIN_1);

    while (1) {
        GPIOPinWrite(GPIO_PORTF_BASE, GPIO_PIN_1,
            ~GPIOPinRead(GPIO_PORTF_BASE, GPIO_PIN_1) & GPIO_PIN_1);
        delay(1000000);
    }
}
