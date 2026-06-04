/**
 * \file    systick.h
 * \brief   SysTick 定时器 API
 *
 * NVIC 寄存器定义在 systick.c 中，此头文件仅声明公共接口。
 */

#ifndef SYSTICK_H
#define SYSTICK_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

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
