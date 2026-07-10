/**
 * @file buzzer.h
 * @brief 蜂鸣器驱动（有源 GPIO / 无源 Timer PWM，宏切换）
 */

#ifndef BUZZER_H
#define BUZZER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

/** 蜂鸣器类型：有源（GPIO 电平）/ 无源（Timer PWM） */
#define BUZZER_TYPE_ACTIVE   0
#define BUZZER_TYPE_PASSIVE  1

#ifndef BUZZER_TYPE
#define BUZZER_TYPE BUZZER_TYPE_ACTIVE
#endif

#if (BUZZER_TYPE != BUZZER_TYPE_ACTIVE) && (BUZZER_TYPE != BUZZER_TYPE_PASSIVE)
#error "BUZZER_TYPE must be BUZZER_TYPE_ACTIVE or BUZZER_TYPE_PASSIVE"
#endif

#if (BUZZER_TYPE == BUZZER_TYPE_ACTIVE)

#ifndef BUZZER_ACTIVE_HIGH
#define BUZZER_ACTIVE_HIGH 1
#endif

#endif /* BUZZER_TYPE_ACTIVE */

#define BUZZER_FREQ_MIN_HZ          100U
#define BUZZER_FREQ_MAX_HZ          20000U
#define BUZZER_DEFAULT_FREQ_HZ      4000U
#define BUZZER_DEFAULT_DUTY_PERCENT 50U

#define BUZZER_DEFAULT_ON_MS        80U
#define BUZZER_DEFAULT_GAP_MS       100U

void buzzer_init(void);
void buzzer_start(uint32_t freq_hz, uint8_t duty_percent);
void buzzer_stop(void);
bool buzzer_beep(uint32_t freq_hz, uint32_t duration_ms);
void buzzer_chirp(uint8_t count, uint32_t on_ms, uint32_t gap_ms);

#ifdef __cplusplus
}
#endif

#endif /* BUZZER_H */
