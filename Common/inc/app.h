/**
 * @file app.h
 * @brief 应用入口声明（双线程实现在 projects/<car>/main/app.c）
 */

#ifndef COMMON_APP_H
#define COMMON_APP_H

#ifdef __cplusplus
extern "C" {
#endif

#include "type.h"

/* -------------------------------------------------------------------------- */
/* FreeRTOS 任务名（≤ 16 字符）                                               */
/* -------------------------------------------------------------------------- */

#define APP_TASK_NAME_EVT   "app_evt"
#define APP_TASK_NAME_TMR   "app_tmr"
#define APP_TASK_NAME_CMD   "app_cmd"

/* -------------------------------------------------------------------------- */
/* 生命周期                                                                   */
/* -------------------------------------------------------------------------- */

status_t App_Start(void);

#ifdef __cplusplus
}
#endif

#endif /* COMMON_APP_H */
