/**
 * \file    main.c
 * \brief   TM4C123GH6PM 项目入口
 *
 * 功能：初始化 Port F，红 LED 以 SysTick 精确延时闪烁。
 *       按 SW1 (PF4) 可切换 LED 颜色。
 *
 * 编译：
 *   powershell -ExecutionPolicy Bypass -File .\scripts\build.ps1
 *   或: make
 */

#include "tm4c123gh6pm.h"
#include "gpio.h"
#include "systick.h"

/* LED 颜色枚举 */
typedef enum {
    LED_COLOR_RED   = LED_RED,
    LED_COLOR_BLUE  = LED_BLUE,
    LED_COLOR_GREEN = LED_GREEN,
} LedColor_t;

static LedColor_t g_active_led = LED_COLOR_RED;

/* ---------------------------------------------------------------------------
 * 初始化 LaunchPad 板上 GPIO (LED + 按键)
 * -------------------------------------------------------------------------*/
static void GPIO_Init(void)
{
    /* 使能 Port F 时钟 */
    SYSCTL_RCGCGPIO_R |= SYSCTL_RCGCGPIO_R5;
    while ((SYSCTL_PRGPIO_R & (1u << 5)) == 0u) { }

    /* 解锁 PF0 (SW2) + Commit */
    GPIO_PORTF_LOCK_R = GPIO_LOCK_KEY;
    GPIO_PORTF_CR_R   |= LED_RED | LED_BLUE | LED_GREEN | SW1 | SW2;

    /* 方向 */
    GPIO_PORTF_DIR_R  |= LED_RED | LED_BLUE | LED_GREEN;
    GPIO_PORTF_DIR_R  &= ~(SW1 | SW2);

    /* 数字使能 + 上拉 */
    GPIO_PORTF_DEN_R  |= LED_RED | LED_BLUE | LED_GREEN | SW1 | SW2;
    GPIO_PORTF_PUR_R  |= SW1 | SW2;
}

/* ---------------------------------------------------------------------------
 * 设置指定 LED 亮起，其余熄灭
 * -------------------------------------------------------------------------*/
static void LED_SetActive(LedColor_t color)
{
    GPIO_PORTF_DATA_R = (uint32_t)color;
    g_active_led = color;
}

/* ---------------------------------------------------------------------------
 * 切换 LED 颜色 (红 → 蓝 → 绿 → 红 …)
 * -------------------------------------------------------------------------*/
static void LED_NextColor(void)
{
    switch (g_active_led) {
    case LED_COLOR_RED:   LED_SetActive(LED_COLOR_BLUE);  break;
    case LED_COLOR_BLUE:  LED_SetActive(LED_COLOR_GREEN); break;
    default:              LED_SetActive(LED_COLOR_RED);    break;
    }
}

/* ---------------------------------------------------------------------------
 * 主函数
 * -------------------------------------------------------------------------*/
int main(void)
{
    GPIO_Init();
    SysTick_Init();
    LED_SetActive(LED_COLOR_RED);

    /* 按键消抖状态 */
    static uint32_t sw1_last = 1u;

    while (1) {
        /* 红灯基础闪烁 (500 ms) */
        GPIOF_RedLED_Toggle();
        SysTick_DelayMs(500);

        /* 检测 SW1 按下 (低电平有效，软件消抖) */
        uint32_t sw1_now = (GPIO_PORTF_DATA_R >> 4) & 1u;
        if (sw1_now == 0u && sw1_last == 1u) {
            /* 防抖延时 */
            SysTick_DelayMs(50);
            /* 再次确认 */
            if (((GPIO_PORTF_DATA_R >> 4) & 1u) == 0u) {
                LED_NextColor();
            }
        }
        sw1_last = sw1_now;
    }
}
