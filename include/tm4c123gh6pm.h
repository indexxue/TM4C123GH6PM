#ifndef TM4C123GH6PM_H
#define TM4C123GH6PM_H

#include <stdint.h>

#define HWREG(x) (*((volatile uint32_t *)(x)))

#define SYSCTL_RCGCGPIO_R HWREG(0x400FE608u)
#define SYSCTL_PRGPIO_R   HWREG(0x400FEA08u)

#define GPIO_PORTF_DATA_R HWREG(0x400253FCu)
#define GPIO_PORTF_DIR_R  HWREG(0x40025400u)
#define GPIO_PORTF_DEN_R  HWREG(0x4002551Cu)
#define GPIO_PORTF_LOCK_R HWREG(0x40025520u)
#define GPIO_PORTF_CR_R   HWREG(0x40025524u)
#define GPIO_PORTF_PUR_R  HWREG(0x40025510u)

#define SYSCTL_RCGCGPIO_R5 (1u << 5)
#define GPIO_LOCK_KEY      0x4C4F434Bu

#define LED_RED   (1u << 1)
#define LED_BLUE  (1u << 2)
#define LED_GREEN (1u << 3)
#define SW1       (1u << 4)

#endif
