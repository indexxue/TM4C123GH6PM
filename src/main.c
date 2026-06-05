/**
 * \file    main.c
 * \brief   ?? SysConfig ???? + ???? (PLL 80 MHz)
 *
 * ??:
 *   1. SysCtlClockSet()  ? ?????? + PLL @ 80 MHz
 *   2. PinoutSet()       ? ? SysConfig CLI ? .syscfg/ ??
 *   3. ???? (LED ?? + ??????)
 *
 * ??????:
 *   - .syscfg/tm4c123gh6pm.syscfg   (SysConfig GUI ??)
 *   - ? .syscfg/project.json        (JSON ????)
 */

#include <stdint.h>
#include <stdbool.h>

/* TivaWare DriverLib API */
#include "inc/hw_memmap.h"
#include "inc/hw_gpio.h"
#include "inc/hw_types.h"
#include "driverlib/sysctl.h"
#include "driverlib/gpio.h"

/* SysConfig ???????? */
#include "pinout.h"

/* ===== ?????? ========================================================
 *
 * TM4C123G LaunchPad ?? 16 MHz ???
 * ?? PLL ??? 400 MHz??? 5 ??????? 80 MHz?
 *
 *   SysCtlClockSet(SYSCTL_SYSDIV_5 | SYSCTL_USE_PLL |
 *                  SYSCTL_OSC_MAIN | SYSCTL_XTAL_16MHZ);
 *
 * ????????????????? SysTick ?????
 * =========================================================================*/

static void Clock_Init(void)
{
    /* ?????? (16 MHz ??) ? PLL (400 MHz) ? ?? (80 MHz) */
    SysCtlClockSet(SYSCTL_SYSDIV_5 | SYSCTL_USE_PLL |
                   SYSCTL_OSC_MAIN | SYSCTL_XTAL_16MHZ);
}

/* ===== ???? (????????) ==========================================
 * 80 MHz ??? 1 ?? ? 80 ?????
 * ????????????????? SysTick ? Timer?
 * =========================================================================*/
static void delay_us(uint32_t us)
{
    /* ????? 5 ?????? 16 ????? (80 MHz / 5 = 16) */
    uint32_t count = us * 16;
    while (count--) {
        __asm volatile ("nop");
    }
}

static void delay_ms(uint32_t ms)
{
    while (ms--) {
        delay_us(1000);
    }
}

/* ===== LED ?? (Port F ???? LED) ======================================*/
typedef enum {
    LED_RED   = GPIO_PIN_1,
    LED_BLUE  = GPIO_PIN_2,
    LED_GREEN = GPIO_PIN_3,
} LedPin_t;

static void LED_Init(void)
{
    /* ?? Port F ?? (PinoutSet ????, ?????????) */
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOF);
    while (!SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOF)) { }

    /* ?? PF0 (SW2 ??) */
    HWREG(GPIO_PORTF_BASE + GPIO_O_LOCK) = GPIO_LOCK_KEY;
    HWREG(GPIO_PORTF_BASE + GPIO_O_CR) |=
        GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3 | GPIO_PIN_4;

    /* ?? LED ??? */
    GPIOPinTypeGPIOOutput(GPIO_PORTF_BASE, LED_RED | LED_BLUE | LED_GREEN);

    /* ??????? + ?? */
    HWREG(GPIO_PORTF_BASE + GPIO_O_DIR) &= ~(GPIO_PIN_0 | GPIO_PIN_4);
    HWREG(GPIO_PORTF_BASE + GPIO_O_DEN) |=  GPIO_PIN_0 | GPIO_PIN_4;
    HWREG(GPIO_PORTF_BASE + GPIO_O_PUR) |=  GPIO_PIN_0 | GPIO_PIN_4;
}

static void LED_Write(uint32_t pins, uint32_t value)
{
    GPIOPinWrite(GPIO_PORTF_BASE, pins, value);
}

static uint32_t SW1_Read(void)
{
    return (GPIOPinRead(GPIO_PORTF_BASE, GPIO_PIN_4) ? 1u : 0u);
}

/* ===== ??? ==============================================================*/
int main(void)
{
    /* ---- 1. ?????? (?? 16 MHz ?? ? PLL ? 80 MHz) ---- */
    Clock_Init();

    /* ---- 2. SysConfig ???????? ---- */
    PinoutSet();            /* ? SysConfig CLI ? .syscfg ?? */

    /* ---- 3. ??????? ---- */
    LED_Init();

    /* ---- 4. ??? ---- */
    uint32_t color = LED_RED;

    while (1) {
        /* ???? LED ?? (500 ms) */
        LED_Write(color, color);
        delay_ms(500);
        LED_Write(color, 0);
        delay_ms(500);

        /* ?? SW1 ?? ? ???? */
        if (SW1_Read() == 0) {
            delay_ms(50);           /* ?? */
            if (SW1_Read() == 0) {
                switch (color) {
                case LED_RED:   color = LED_BLUE;  break;
                case LED_BLUE:  color = LED_GREEN; break;
                default:        color = LED_RED;   break;
                }
            }
            /* ???? */
            while (SW1_Read() == 0) { }
        }
    }
}
