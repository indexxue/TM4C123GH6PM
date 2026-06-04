/**
 * \file    main.c
 * \brief   TM4C123GH6PM ??????
 *
 * GPIO ???? gen-board-config.py ? .syscfg/project.json ?????
 * ??????? .syscfg/project.json?????? src/generated/ ?????
 *
 * ???????
 *   ?? 500ms ???? SW1 (PF4) ?? LED ????????????
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

/* ===== LED ?? ============================================================ */

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

/* ===== ???? ============================================================ */

static uint32_t DebounceSW1(void)
{
    /* ??????50 ms ??????? */
    if (((GPIO_PORTF_DATA_R >> 4) & 1u) != 0u)
        return 0u;

    SysTick_DelayMs(50);

    return (((GPIO_PORTF_DATA_R >> 4) & 1u) == 0u) ? 1u : 0u;
}

/* ===== ??? ============================================================== */

int main(void)
{
    /* ---- ??????? .syscfg/project.json ?? ---- */
    Board_Init();
    SysTick_Init();
    LED_SetActive(LED_COLOR_RED);

    uint32_t sw1_last = 1u;

    /* ---- ??? ---- */
    while (1) {
        /* ?? 500 ms ?? */
        GPIOF_RedLED_Toggle();
        SysTick_DelayMs(500);

        /* ?? SW1 ?? ? ?? LED ?? */
        uint32_t sw1_now = (GPIO_PORTF_DATA_R >> 4) & 1u;
        if (sw1_now == 0u && sw1_last == 1u) {
            if (DebounceSW1()) {
                LED_NextColor();
            }
        }
        sw1_last = sw1_now;
    }
}
