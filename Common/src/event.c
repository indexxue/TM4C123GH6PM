/**
 * @file event.c
 * @brief 全局事件位掩码调度实现
 */

#include "event.h"

#include "log.h"

#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

#include <string.h>

typedef struct {
    uint32_t mask;
    SemaphoreHandle_t semaphore;
    SemaphoreHandle_t mutex;
} event_ctx_t;

static event_ctx_t *s_event = NULL;

status_t event_init(void)
{
    if (s_event != NULL) {
        return STATUS_OK;
    }

    static event_ctx_t local;
    s_event = &local;
    (void)memset(s_event, 0, sizeof(event_ctx_t));

    s_event->semaphore = xSemaphoreCreateBinary();
    if (s_event->semaphore == NULL) {
        LOG_ERROR("event: semaphore create failed");
        s_event = NULL;
        return STATUS_NO_MEM;
    }

    s_event->mutex = xSemaphoreCreateMutex();
    if (s_event->mutex == NULL) {
        LOG_ERROR("event: mutex create failed");
        vSemaphoreDelete(s_event->semaphore);
        s_event = NULL;
        return STATUS_NO_MEM;
    }

    return STATUS_OK;
}

void event_set(evt_id_t id)
{
    if ((s_event == NULL) || (id == EVT_ID_NONE)) {
        return;
    }

    if (xSemaphoreTake(s_event->mutex, portMAX_DELAY) != pdTRUE) {
        return;
    }

    if ((s_event->mask & (uint32_t)id) != 0U) {
        (void)xSemaphoreGive(s_event->mutex);
        return;
    }

    s_event->mask |= (uint32_t)id;
    (void)xSemaphoreGive(s_event->mutex);
    (void)xSemaphoreGive(s_event->semaphore);
}

void event_set_from_isr(evt_id_t id)
{
    BaseType_t wake = pdFALSE;

    if ((s_event == NULL) || (id == EVT_ID_NONE)) {
        return;
    }

    if (xSemaphoreTakeFromISR(s_event->mutex, &wake) != pdTRUE) {
        portYIELD_FROM_ISR(wake);
        return;
    }

    if ((s_event->mask & (uint32_t)id) != 0U) {
        (void)xSemaphoreGiveFromISR(s_event->mutex, &wake);
        portYIELD_FROM_ISR(wake);
        return;
    }

    s_event->mask |= (uint32_t)id;
    (void)xSemaphoreGiveFromISR(s_event->mutex, &wake);
    (void)xSemaphoreGiveFromISR(s_event->semaphore, &wake);
    portYIELD_FROM_ISR(wake);
}

bool event_is_set(evt_id_t id)
{
    bool pending = false;

    if ((s_event == NULL) || (id == EVT_ID_NONE)) {
        return false;
    }

    if (xSemaphoreTake(s_event->mutex, 0) != pdTRUE) {
        return false;
    }

    if ((s_event->mask & (uint32_t)id) != 0U) {
        s_event->mask &= ~(uint32_t)id;
        pending = true;
    }

    (void)xSemaphoreGive(s_event->mutex);
    return pending;
}

void event_clear(evt_id_t id)
{
    if ((s_event == NULL) || (id == EVT_ID_NONE)) {
        return;
    }

    if (xSemaphoreTake(s_event->mutex, 0) != pdTRUE) {
        return;
    }

    s_event->mask &= ~(uint32_t)id;
    (void)xSemaphoreGive(s_event->mutex);
}

void event_schedule(void)
{
    if (s_event == NULL) {
        return;
    }

    (void)xSemaphoreTake(s_event->semaphore, portMAX_DELAY);
}
