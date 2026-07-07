/**
 * @file event.h
 * @brief 全局事件位掩码调度（FreeRTOS 二值信号量 + 互斥锁）
 */

#ifndef COMMON_EVENT_H
#define COMMON_EVENT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "type.h"

/* -------------------------------------------------------------------------- */
/* 事件 ID（位掩码，可 OR 组合）                                               */
/* -------------------------------------------------------------------------- */

typedef enum {
    EVT_ID_NONE     = 0x00000000U,
    EVT_ID_BUTTON   = 0x00000001U,
    EVT_ID_INPUT    = 0x00000002U,
    EVT_ID_TIMER    = 0x00000004U,
    EVT_ID_WATCHDOG = 0x00000008U,
} evt_id_t;

status_t event_init(void);
void event_set(evt_id_t id);
void event_set_from_isr(evt_id_t id);
bool event_is_set(evt_id_t id);
void event_clear(evt_id_t id);
void event_schedule(void);

#ifdef __cplusplus
}
#endif

#endif /* COMMON_EVENT_H */
