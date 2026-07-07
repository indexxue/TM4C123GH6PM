/**
 * @file    factory_init.c
 * @brief   厂测固件板级与外设初始化
 */

#include "factory.h"

#include "board.h"
#include "log.h"
#include "cmd.h"
#include "flash_layout.h"

#include "driverlib/sysctl.h"

static void Clock_Init(void)
{
    SysCtlClockSet(SYSCTL_SYSDIV_5 | SYSCTL_USE_PLL |
                   SYSCTL_OSC_MAIN | SYSCTL_XTAL_16MHZ);
}

void Factory_Board_Init(void)
{
    Clock_Init();
    Motor_Init();
    Encoder_Init();
    Line_Init();
    Board_Periph_Init();

    (void)log_init(NULL);
    (void)cmd_uart_line_service_start();

    LOG_INFO("factory: init @ 0x%08lX (APP_B storage)",
             (unsigned long)FLASH_APP_B_BASE);
}
