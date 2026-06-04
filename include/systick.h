/**
 * \file    systick.h
 * \brief   SysTick 定时器 API
 *
 * NVIC SysTick 寄存器定义包含在此头文件内，
 * 避免对 SDK 头文件产生依赖。
 */

#ifndef SYSTICK_H
#define SYSTICK_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========== SysTick 寄存器定义 ============================================*/
#define NVIC_ST_BASE          0xE000E000u

#define HWREG(x)             (*((volatile uint32_t *)(x)))
#define NVIC_ST_CSR           HWREG(NVIC_ST_BASE + 0x010u)
#define NVIC_ST_RELOAD        HWREG(NVIC_ST_BASE + 0x014u)
#define NVIC_ST_CURRENT       HWREG(NVIC_ST_BASE + 0x018u)

#define NVIC_ST_CSR_ENABLE    (1u << 0)
#define NVIC_ST_CSR_INTEN     (1u << 1)
#define NVIC_ST_CSR_CLK_SRC   (1u << 2)
#define NVIC_ST_CSR_COUNTFLAG (1u << 16)

/* ========== API 原型 ======================================================*/

/** 初始化 SysTick (1 ms 中断周期) */
void SysTick_Init(void);

/** 阻塞延时 (毫秒) */
void SysTick_DelayMs(uint32_t ms);

/** 获取 SysTick 中断计数值 (从 0 开始累加) */
uint32_t SysTick_GetTicks(void);

#ifdef __cplusplus
}
#endif

#endif /* SYSTICK_H */
