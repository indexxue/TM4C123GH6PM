/**
 * \file    main.c
 * \brief   TM4C123GH6PM 项目入口
 *
 * GPIO 初始化由 SysConfig 生成 (src/generated/tm4c123_board.c)，
 * 应用层只负责逻辑。配置来源: .syscfg/project.json
 */

#include "tm4c123gh6pm.h"
#include "gpio.h"
#include "systick.h"
#include "tm4c123_board.h"
extern void Board_Init(void);

typedef enum {
    LED_COLOR_RED   = LED_RED,
    LED_COLOR_BLUE  = LED_BLUE,
    LED_COLOR_GREEN = LED_GREEN,
} LedColor_t;

static LedColor_t g_active_led = LED_COLOR_RED;

static void LED_SetActive(LedColor_t color)
{
    GPIO_PORTF_DATA_R = (uint32_t)color;
    g_active_led = color;
}

static void LED_NextColor(void)
{
    switch (g_active_led) {
    case LED_COLOR_RED:   LED_SetActive(LED_COLOR_BLUE);  break;
    case LED_COLOR_BLUE:  LED_SetActive(LED_COLOR_GREEN); break;
    default:              LED_SetActive(LED_COLOR_RED);    break;
    }
}

int main(void)
{
    Board_Init();                        /* 由 gen-board-config.py 生成 */
    SysTick_Init();
    LED_SetActive(LED_COLOR_RED);

    static uint32_t sw1_last = 1u;

    while (1) {
        GPIOF_RedLED_Toggle();
        SysTick_DelayMs(500);

        uint32_t sw1_now = (GPIO_PORTF_DATA_R >> 4) & 1u;
        if (sw1_now == 0u && sw1_last == 1u) {
            SysTick_DelayMs(50);
            if (((GPIO_PORTF_DATA_R >> 4) & 1u) == 0u) {
                LED_NextColor();
            }
        }
        sw1_last = sw1_now;
    }
}