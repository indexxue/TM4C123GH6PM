/**
 * @file clock.c
 * @brief TM4C123 系统时钟初始化
 */

#include "clock.h"

#include <stdbool.h>
#include <stdint.h>

#include "driverlib/sysctl.h"

void bsp_clock_init(bsp_clock_source_t source)
{
    switch (source) {
    case BSP_CLOCK_MAIN_8MHZ:
        /* 8 MHz XTAL + PLL /2.5 → 80 MHz（实测 SYSDIV_4=50M，SYSDIV_5=40M） */
        SysCtlClockSet(SYSCTL_SYSDIV_2_5 | SYSCTL_USE_PLL |
                       SYSCTL_OSC_MAIN | SYSCTL_XTAL_8MHZ);
        break;
    case BSP_CLOCK_MAIN_16MHZ:
        SysCtlClockSet(SYSCTL_SYSDIV_5 | SYSCTL_USE_PLL |
                       SYSCTL_OSC_MAIN | SYSCTL_XTAL_16MHZ);
        break;
    case BSP_CLOCK_INT_PIOSC:
        SysCtlClockSet(SYSCTL_SYSDIV_5 | SYSCTL_USE_PLL | SYSCTL_OSC_INT |
                       SYSCTL_MAIN_OSC_DIS);
        break;
    default:
        SysCtlClockSet(SYSCTL_SYSDIV_2_5 | SYSCTL_USE_PLL |
                       SYSCTL_OSC_MAIN | SYSCTL_XTAL_8MHZ);
        break;
    }
}

uint32_t bsp_clock_get_hz(void)
{
    return SysCtlClockGet();
}
