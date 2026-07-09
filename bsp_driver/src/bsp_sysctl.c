/**
 * @file bsp_sysctl.c
 * @brief TM4C123 系统控制 / 时钟
 */

#include "bsp_sysctl.h"

#include "bsp_systick.h"

#include "bsp_config.h"

#include "driverlib/sysctl.h"
#include "driverlib/watchdog.h"

#include "inc/hw_memmap.h"
#include "inc/hw_nvic.h"
#include "inc/hw_types.h"

#define BSP_DEMCR_ADDR          0xE000EDFCU
#define BSP_DEMCR_VC_CORERESET  0x00000001U

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

void bsp_system_reset(void)
{
    __asm volatile("cpsid i" ::: "memory");

    /* 停 SysTick，避免 RTOS tick 干扰复位握手 */
    HWREG(NVIC_ST_CTRL) = 0U;

    /*
     * J-Link / OpenOCD 连接时 DEMCR.VC_CORERESET 会拦截 AIRCR 软件复位，
     * 表现即为 SysCtlReset() 返回不了、设备“卡死”。量产无调试器时通常无此问题。
     */
    HWREG(BSP_DEMCR_ADDR) &= ~BSP_DEMCR_VC_CORERESET;

    __asm volatile("dsb 0xF" ::: "memory");
    HWREG(NVIC_APINT) = NVIC_APINT_VECTKEY | NVIC_APINT_SYSRESETREQ;
    __asm volatile("dsb 0xF" ::: "memory");
    __asm volatile("isb 0xF" ::: "memory");

    /* AIRCR 仍无效时（极端情况），看门狗强制复位 */
    SysCtlPeripheralEnable(SYSCTL_PERIPH_WDOG0);
    while (!SysCtlPeripheralReady(SYSCTL_PERIPH_WDOG0)) {
    }
    WatchdogUnlock(WATCHDOG0_BASE);
    WatchdogReloadSet(WATCHDOG0_BASE, 1U);
    WatchdogResetEnable(WATCHDOG0_BASE);
    WatchdogEnable(WATCHDOG0_BASE);
    WatchdogLock(WATCHDOG0_BASE);

    for (;;) {
    }
}
