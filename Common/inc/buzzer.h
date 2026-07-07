/**
 * @file buzzer.h
 * @brief 无源蜂鸣器 PWM 驱动（Timer CCP @ GPIO_BUZZER_*）
 */

#ifndef BUZZER_H
#define BUZZER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

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
