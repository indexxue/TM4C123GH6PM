/**
 * @file bsp_systick.c
 * @brief DWT 周期计数延时；调度器运行后毫秒延时走 FreeRTOS
 */

#include "bsp_systick.h"

#include "bsp_sysctl.h"

#include "bsp_config.h"

#include "FreeRTOS.h"
#include "task.h"

#define DWT_CTRL   (*(volatile uint32_t *)0xE0001000u)
#define DWT_CYCCNT (*(volatile uint32_t *)0xE0001004u)
#define DEM_CR     (*(volatile uint32_t *)0xE000EDFCu)

#define DEM_CR_TRCENA      (1u << 24)
#define DWT_CTRL_CYCCNTENA (1u << 0)

static bool s_dwt_ready;

void bsp_systick_init(void)
{
    DEM_CR |= DEM_CR_TRCENA;
    DWT_CYCCNT = 0U;
    DWT_CTRL |= DWT_CTRL_CYCCNTENA;
    s_dwt_ready = true;
}

void bsp_delay_us(uint32_t us)
{
    uint32_t clock_hz = bsp_clock_get_hz();
    uint32_t cycles_per_us = clock_hz / 1000000U;
    uint32_t start;
    uint32_t target;

    if (!s_dwt_ready || (clock_hz == 0U) || (cycles_per_us == 0U)) {
        return;
    }

    start = DWT_CYCCNT;
    target = us * cycles_per_us;
    while ((DWT_CYCCNT - start) < target) {
    }
}

void bsp_delay_ms(uint32_t ms)
{
    if ((xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) && (ms > 0U)) {
        vTaskDelay(pdMS_TO_TICKS(ms));
        return;
    }

    while (ms-- > 0U) {
        bsp_delay_us(1000U);
    }
}

uint32_t bsp_get_tick_ms(void)
{
    if (xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED) {
        return 0U;
    }

    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

uint32_t bsp_get_tick_us(void)
{
    uint32_t clock_hz = bsp_clock_get_hz();
    uint32_t cycles_per_us = (clock_hz > 0U) ? (clock_hz / 1000000U) : 0U;

    if (!s_dwt_ready || (cycles_per_us == 0U)) {
        return 0U;
    }

    return DWT_CYCCNT / cycles_per_us;
}

void bsp_timeout_start_us(bsp_timeout_t *t, uint32_t timeout_us)
{
    uint32_t clock_hz;
    uint32_t cycles_per_us;

    if (t == NULL) {
        return;
    }

    clock_hz = bsp_clock_get_hz();
    cycles_per_us = (clock_hz > 0U) ? (clock_hz / 1000000U) : 0U;
    if (!s_dwt_ready || (cycles_per_us == 0U) || (timeout_us == 0U)) {
        t->start_cycles = 0U;
        t->limit_cycles = 0U;
        return;
    }

    t->start_cycles = DWT_CYCCNT;
    t->limit_cycles = timeout_us * cycles_per_us;
}

bool bsp_timeout_expired(const bsp_timeout_t *t)
{
    if ((t == NULL) || (t->limit_cycles == 0U)) {
        return true;
    }

    return (DWT_CYCCNT - t->start_cycles) >= t->limit_cycles;
}

bool bsp_dwt_is_ready(void)
{
    return s_dwt_ready;
}
