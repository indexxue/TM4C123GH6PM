/**
 * @file    factory.h
 * @brief   厂测固件公共接口（链 APP_B @ 0x00021000，不直接运行）
 */

#ifndef FACTORY_H
#define FACTORY_H

#ifdef __cplusplus
extern "C" {
#endif

#include "type.h"

/* -------------------------------------------------------------------------- */
/* FreeRTOS 任务名（≤ 16 字符）                                               */
/* -------------------------------------------------------------------------- */

#define FACTORY_TASK_NAME_EVT   "factory_evt"
#define FACTORY_TASK_NAME_TMR   "factory_tmr"

/* -------------------------------------------------------------------------- */
/* 生命周期                                                                   */
/* -------------------------------------------------------------------------- */

void Factory_Board_Init(void);
status_t Factory_Start(void);

#ifdef __cplusplus
}
#endif

#endif /* FACTORY_H */
