/**
 * \file    systick.c
 * \brief   SysTick 定时器驱动 (阻塞式延时 + 中断节拍)
 */

#include "systick.h"
#include "systick.h"

/* 系统时钟频率 (默认 16 MHz 内部振荡器) */
#ifndef SYSTICK_CLOCK_HZ
#define SYSTICK_CLOCK_HZ  16000000u
#endif

static volatile uint32_t g_systick_ticks = 0u;

/* ---------------------------------------------------------------------------
 * 初始化 SysTick (使用内部时钟源，使能中断)
 * -------------------------------------------------------------------------*/
void SysTick_Init(void)
{
    /* 设置重载值：1 ms 一次中断 */
    NVIC_ST_RELOAD  = (SYSTICK_CLOCK_HZ / 1000u) - 1u;
    NVIC_ST_CURRENT = 0u;

    /* 使能 SysTick：时钟源 = 系统时钟，使能中断 */
    NVIC_ST_CSR = NVIC_ST_CSR_ENABLE | NVIC_ST_CSR_INTEN | NVIC_ST_CSR_CLK_SRC;
}

/* ---------------------------------------------------------------------------
 * SysTick 中断处理函数 (weak 别名已在 startup 中定义)
 * -------------------------------------------------------------------------*/
void SysTick_Handler(void)
{
    g_systick_ticks++;
}

/* ---------------------------------------------------------------------------
 * 毫秒级阻塞延时 (基于 SysTick 中断计数)
 * -------------------------------------------------------------------------*/
void SysTick_DelayMs(uint32_t ms)
{
    uint32_t start = g_systick_ticks;
    while ((g_systick_ticks - start) < ms) {
        __asm volatile ("wfi");     /* 等待中断，降低功耗 */
    }
}

/* ---------------------------------------------------------------------------
 * 获取当前 SysTick 毫秒计数
 * -------------------------------------------------------------------------*/
uint32_t SysTick_GetTicks(void)
{
    return g_systick_ticks;
}
