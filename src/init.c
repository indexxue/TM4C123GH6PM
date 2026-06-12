/**
 * \file    init.c
 * \brief   时钟、引脚、外设与 Common 模块初始化
 *
 * 引脚由 .syscfg/project.json → gen-board-config.py 生成 (Common/pinout)。
 * 外设 Init 由 gen-car-config.py 生成 (Common/peripheral)。
 */

#include "init.h"

#include "pinout.h"
#include "peripheral.h"
#include "log.h"
#include "cmd.h"
#include "ota_meta.h"

#include "driverlib/sysctl.h"

/** 系统时钟 80 MHz（16 MHz 晶振 + PLL），须最先执行。 */
static void Clock_Init(void)
{
    SysCtlClockSet(SYSCTL_SYSDIV_5 | SYSCTL_USE_PLL |
                   SYSCTL_OSC_MAIN | SYSCTL_XTAL_16MHZ);
}

void Board_Init(void)
{
    Clock_Init();
    PinoutSet();

    Motor_Init();
    Encoder_Init();
    UART_Init();
    I2C_Init();
    SSI_Init();
    UART_Debug_Init();
    DMA_Init();
    ADC_Init();

    (void)log_init(NULL);
    (void)ota_init();
    (void)cmd_uart_line_service_start();
}
