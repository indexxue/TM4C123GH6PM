/**
 * \file    main.c
 * \brief   TM4C123GH6PM 小车底盘主程序
 *
 * 引脚配置由 .syscfg/project.json 通过 gen-board-config.py 生成。
 * 外设初始化由 car_config.c/h 提供 (Motor/Encoder/UART/I2C/ADC)。
 * 详见 docs/resource-allocation-pro.md
 */

#include <stdint.h>
#include <stdbool.h>
#include "inc/hw_memmap.h"
#include "inc/hw_gpio.h"
#include "inc/hw_types.h"
#include "driverlib/sysctl.h"
#include "driverlib/gpio.h"
#include "driverlib/timer.h"

#include "pinout.h"         /* 由 SysConfig CLI 或 Python 回退生成的引脚初始化 */
#include "car_config.h"     /* 由 gen-board-config.py 生成的外设配置 */

/* ===== 系统时钟 ============================================================ */
static void Clock_Init(void)
{
    /* 16 MHz 晶振 -> PLL (400 MHz) -> 分频 5 -> 80 MHz */
    SysCtlClockSet(SYSCTL_SYSDIV_5 | SYSCTL_USE_PLL |
                   SYSCTL_OSC_MAIN | SYSCTL_XTAL_16MHZ);
}

/* ===== LED 板载指示 ======================================================= */
static void LED_Init(void)
{
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOF);
    while (!SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOF)) { }
    HWREG(GPIO_PORTF_BASE + GPIO_O_LOCK) = GPIO_LOCK_KEY;
    HWREG(GPIO_PORTF_BASE + GPIO_O_CR) |=
        GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3 | GPIO_PIN_4;
    GPIOPinTypeGPIOOutput(GPIO_PORTF_BASE, GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3);
}

/* ===== 电机方向控制（简易封装）============================================== */
static inline void Motor_SetDir(uint32_t base, uint8_t pin_in1, uint8_t pin_in2,
                                int8_t dir /* -1=后退, 0=停止, 1=前进 */)
{
    switch (dir) {
    case 1:  GPIOPinWrite(base, pin_in1 | pin_in2, pin_in1); break;
    case -1: GPIOPinWrite(base, pin_in1 | pin_in2, pin_in2); break;
    default: GPIOPinWrite(base, pin_in1 | pin_in2, 0);       break;
    }
}

/* ===== 主函数 ============================================================== */
int main(void)
{
    /* ---- 系统初始化 ---- */
    Clock_Init();
    PinoutSet();        /* GPIO 引脚配置 */
    LED_Init();

    /* ---- 外设初始化 ---- */
    Motor_Init();       /* PWM 定时器 */
    Encoder_Init();     /* 编码器捕获 */
    UART_Init();        /* 蓝牙 */
    I2C_Init();         /* IMU + OLED */
    SSI_Init();         /* Magnetometer */
    UART_Debug_Init();  /* Debug serial (TX only) */
    DMA_Init();         /* uDMA */
    ADC_Init();         /* 电池检测 */

    /* 指示灯：红灯亮表示系统就绪 */
    GPIOPinWrite(GPIO_PORTF_BASE, GPIO_PIN_1, GPIO_PIN_1);

    /* ---- 主循环 ---- */
    // uint32_t tick

    while (1) {
        /* 20 ms 控制周期 (50 Hz) */
        SysCtlDelay(SysCtlClockGet() / 50 / 3);

        /* TODO: 读取蓝牙指令 */
        /* TODO: IMU 姿态更新 */
        /* TODO: 编码器速度计算 */
        /* TODO: PID 控制器 */
        /* TODO: 输出电机 PWM */

        /* 指示灯闪烁心跳 */
        GPIOPinWrite(GPIO_PORTF_BASE, GPIO_PIN_1,
            ~GPIOPinRead(GPIO_PORTF_BASE, GPIO_PIN_1) & GPIO_PIN_1);
    }
}