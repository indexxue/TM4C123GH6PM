/**
 * \file    main.c
 * \brief   TM4C123GH6PM 小车底盘主程序 (FreeRTOS)
 *
 * 引脚配置由 .syscfg/project.json 通过 gen-board-config.py 生成。
 * 外设初始化由 car_config.c/h 提供 (Motor/Encoder/UART/I2C/ADC)。
 * 应用逻辑在 app_tasks.c 各 FreeRTOS 任务中运行。
 * 详见 docs/resource-allocation-pro.md
 */

#include <stdint.h>
#include <stdbool.h>
#include "FreeRTOS.h"
#include "task.h"
#include "inc/hw_memmap.h"
#include "inc/hw_gpio.h"
#include "inc/hw_types.h"
#include "driverlib/sysctl.h"
#include "driverlib/gpio.h"

#include "pinout.h"
#include "car_config.h"
#include "app_tasks.h"

static void Clock_Init(void)
{
    SysCtlClockSet(SYSCTL_SYSDIV_5 | SYSCTL_USE_PLL |
                   SYSCTL_OSC_MAIN | SYSCTL_XTAL_16MHZ);
}

static void LED_Init(void)
{
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOF);
    while (!SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOF)) { }
    HWREG(GPIO_PORTF_BASE + GPIO_O_LOCK) = GPIO_LOCK_KEY;
    HWREG(GPIO_PORTF_BASE + GPIO_O_CR) |=
        GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3 | GPIO_PIN_4;
    GPIOPinTypeGPIOOutput(GPIO_PORTF_BASE, GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3);
}

int main(void)
{
    Clock_Init();
    PinoutSet();
    LED_Init();

    Motor_Init();
    Encoder_Init();
    UART_Init();
    I2C_Init();
    SSI_Init();
    UART_Debug_Init();
    DMA_Init();
    ADC_Init();

    GPIOPinWrite(GPIO_PORTF_BASE, GPIO_PIN_1, GPIO_PIN_1);

    AppTasks_Create();
    vTaskStartScheduler();

    for (;;) {
    }
}
