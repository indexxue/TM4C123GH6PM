#ifndef PERIPHERAL_H
#define PERIPHERAL_H
#include <stdint.h>
#define SYSCLK_HZ    80000000
#define PWM_FREQ_HZ  10000
#define PWM_PERIOD   (SYSCLK_HZ / PWM_FREQ_HZ)

#define M1_TIMER  TIMER0_BASE
#define M1_PWM_CH TIMER_A
#define M2_TIMER  TIMER0_BASE
#define M2_PWM_CH TIMER_B
#define M3_TIMER  TIMER2_BASE
#define M3_PWM_CH TIMER_A
#define M4_TIMER  TIMER2_BASE
#define M4_PWM_CH TIMER_B

void Motor_Init(void);
void Encoder_Init(void);
void UART_Init(void);
void UART_Putc(char c);
void UART_Puts(const char* s);
int UART_Getc(char *c);
void I2C_Init(void);
void ADC_Init(void);
void SSI_Init(void);
void DMA_Init(void);
void UART_Debug_Init(void);
void UART_Debug_Putc(char c);
void UART_Debug_Puts(const char* s);
int UART_Debug_Getc(char *c);
#endif