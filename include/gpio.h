/**
 * \file    gpio.h
 * \brief   TM4C123GH6PM GPIO 便捷宏和函数
 *
 * 用法示例：
 *   GPIO_SetPin(GPIO_PORTF_BASE, 1);      // PF1 = 高
 *   GPIO_TogglePin(GPIO_PORTF_BASE, 1);    // PF1 翻转
 */

#ifndef GPIO_H
#define GPIO_H

#include <stdint.h>
#include "tm4c123gh6pm.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========== GPIO 基地址 (Port F) ==========================================*/
#define GPIO_PORTF_BASE         0x40025000u

/* ========== 引脚级位操作 ===================================================*/

/** 引脚置高 */
#define GPIO_SetPin(base, pin)      HWREG(base + (0x400u | (pin << 2))) = 0xFFu

/** 引脚拉低 */
#define GPIO_ClrPin(base, pin)      HWREG(base + (0x400u | (pin << 2))) = 0x00u

/** 读取引脚电平 */
#define GPIO_ReadPin(base, pin)     (HWREG(base + (0x400u | (pin << 2))) & 0xFFu)

/** 翻转引脚 */
#define GPIO_TogglePin(base, pin)   HWREG(base + (0x400u | (pin << 2))) ^= 0xFFu

/* ========== TM4C123G LaunchPad 快捷宏 ======================================*/

/* PF1=红, PF2=蓝, PF3=绿, PF4=SW1, PF0=SW2 */
#define SW2                       (1u << 0)

#define GPIOF_RedLED_On()       GPIO_SetPin(GPIO_PORTF_BASE, 1)
#define GPIOF_RedLED_Off()      GPIO_ClrPin(GPIO_PORTF_BASE, 1)
#define GPIOF_RedLED_Toggle()   GPIO_TogglePin(GPIO_PORTF_BASE, 1)

#define GPIOF_BlueLED_On()      GPIO_SetPin(GPIO_PORTF_BASE, 2)
#define GPIOF_BlueLED_Off()     GPIO_ClrPin(GPIO_PORTF_BASE, 2)
#define GPIOF_BlueLED_Toggle()  GPIO_TogglePin(GPIO_PORTF_BASE, 2)

#define GPIOF_GreenLED_On()     GPIO_SetPin(GPIO_PORTF_BASE, 3)
#define GPIOF_GreenLED_Off()    GPIO_ClrPin(GPIO_PORTF_BASE, 3)
#define GPIOF_GreenLED_Toggle() GPIO_TogglePin(GPIO_PORTF_BASE, 3)

#define GPIOF_SW1_Read()        GPIO_ReadPin(GPIO_PORTF_BASE, 4)
#define GPIOF_SW2_Read()        GPIO_ReadPin(GPIO_PORTF_BASE, 0)

#ifdef __cplusplus
}
#endif

#endif /* GPIO_H */
