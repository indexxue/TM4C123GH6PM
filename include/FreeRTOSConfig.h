/**
 * \file    FreeRTOSConfig.h
 * \brief   FreeRTOS 内核配置 (TM4C123GH6PM @ 80 MHz)
 */

#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#include <stdint.h>

/* 80 MHz system clock (must match Clock_Init / project.json) */
#define configCPU_CLOCK_HZ              (80000000UL)
#define configTICK_RATE_HZ              (1000UL)

#define configUSE_PREEMPTION            1
#define configUSE_IDLE_HOOK             0
#define configUSE_TICK_HOOK             0
#define configUSE_16_BIT_TICKS          0
#define configIDLE_SHOULD_YIELD         1

#define configMAX_PRIORITIES            (5U)
#define configMINIMAL_STACK_SIZE        (128U)
#define configMAX_TASK_NAME_LEN         (16U)
#define configTOTAL_HEAP_SIZE           (10240U)

#define configUSE_MUTEXES               1
#define configUSE_RECURSIVE_MUTEXES     1
#define configUSE_COUNTING_SEMAPHORES   1
#define configUSE_TIMERS                0
#define configUSE_CO_ROUTINES           0
#define configUSE_QUEUE_SETS            0

#define configCHECK_FOR_STACK_OVERFLOW  2
#define configUSE_TRACE_FACILITY        0
#define configUSE_STATS_FORMATTING_FUNCTIONS 0
#define configGENERATE_RUN_TIME_STATS   0

#define INCLUDE_vTaskPrioritySet        1
#define INCLUDE_uxTaskPriorityGet       1
#define INCLUDE_vTaskDelete             1
#define INCLUDE_vTaskSuspend            1
#define INCLUDE_vTaskDelay              1
#define INCLUDE_vTaskDelayUntil         1
#define INCLUDE_xTaskGetCurrentTaskHandle 1
#define INCLUDE_uxTaskGetStackHighWaterMark 1

/*
 * Cortex-M4 NVIC priority (3 bits implemented, upper 3 bits of byte).
 * Kernel runs at lowest priority; ISRs above priority 5 must not call
 * FreeRTOS FromISR APIs.
 */
#define configKERNEL_INTERRUPT_PRIORITY         (7U << 5U)
#define configMAX_SYSCALL_INTERRUPT_PRIORITY    (5U << 5U)

#define configASSERT(x) \
    do { if ((x) == 0) { taskDISABLE_INTERRUPTS(); for (;;) { } } } while (0)

#endif /* FREERTOS_CONFIG_H */
