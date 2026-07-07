/**
 * \file    systick.c
 * \brief   SysTick 定时器驱动 (阻塞式延时 + 中断节拍)
 */

#include "tm4c123gh6pm.h"    /* 提供 HWREG 宏 */
#include "systick.h"

/* ========== NVIC SysTick 寄存器 ==========================================*/
#define NVIC_ST_BASE          0xE000E000u
#define NVIC_ST_CSR           HWREG(NVIC_ST_BASE + 0x010u)
#define NVIC_ST_RELOAD        HWREG(NVIC_ST_BASE + 0x014u)
#define NVIC_ST_CURRENT       HWREG(NVIC_ST_BASE + 0x018u)

#define NVIC_ST_CSR_ENABLE    (1u << 0)
#define NVIC_ST_CSR_INTEN     (1u << 1)
#define NVIC_ST_CSR_CLK_SRC   (1u << 2)
#define NVIC_ST_CSR_COUNTFLAG (1u << 16)

/* 系统时钟频率 (默认 16 MHz 内部振荡器) */
#ifndef SYSTICK_CLOCK_HZ
#define SYSTICK_CLOCK_HZ      16000000u
#endif

static volatile uint32_t g_systick_ticks = 0u;

/* ======================================================================== */

void SysTick_Init(void)
{
    NVIC_ST_RELOAD  = (SYSTICK_CLOCK_HZ / 1000u) - 1u;
    NVIC_ST_CURRENT = 0u;

    /* 时钟源 = 系统时钟，使能中断，启动计数 */
    NVIC_ST_CSR = NVIC_ST_CSR_ENABLE | NVIC_ST_CSR_INTEN | NVIC_ST_CSR_CLK_SRC;
}

/* ---------------------------------------------------------------------------
 * SysTick 中断处理函数 (weak 别名已在 startup 中定义)
 * -------------------------------------------------------------------------*/
void SysTick_Handler(void)
{
    g_systick_ticks++;
}

void SysTick_DelayMs(uint32_t ms)
{
    uint32_t start = g_systick_ticks;
    while ((g_systick_ticks - start) < ms) {
        __asm volatile ("wfi");     /* 等待中断，降低功耗 */
    }
}

uint32_t SysTick_GetTicks(void)
{
    return g_systick_ticks;
}
