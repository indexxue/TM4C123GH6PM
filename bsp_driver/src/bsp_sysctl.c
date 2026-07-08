/**
 * @file bsp_sysctl.c
 * @brief TM4C123 系统控制 / 时钟
 */

#include "bsp_sysctl.h"

#include "bsp_systick.h"

#include "bsp_config.h"

#include "driverlib/sysctl.h"

void bsp_clock_init(bsp_clock_source_t source)
{
    switch (source) {
    case BSP_CLOCK_INT_PIOSC:
        SysCtlClockSet(SYSCTL_SYSDIV_5 | SYSCTL_USE_PLL | SYSCTL_OSC_INT |
                       SYSCTL_MAIN_OSC_DIS);
        break;
    case BSP_CLOCK_MAIN_8MHZ:
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

bool bsp_periph_wait_ready(uint32_t periph, uint32_t timeout_us)
{
    bsp_timeout_t timeout;

    if (!bsp_dwt_is_ready()) {
        while (!SysCtlPeripheralReady(periph)) {
        }
        return true;
    }

    bsp_timeout_start_us(&timeout, timeout_us);
    while (!SysCtlPeripheralReady(periph)) {
        if (bsp_timeout_expired(&timeout)) {
            return false;
        }
    }

    return true;
}
