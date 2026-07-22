#include <stdint.h>

extern uint32_t _estack;
extern uint32_t _sidata;
extern uint32_t _sdata;
extern uint32_t _edata;
extern uint32_t _sbss;
extern uint32_t _ebss;

int main(void);
void Reset_Handler(void);
void Default_Handler(void);

/* FreeRTOS Cortex-M4F port handlers */
void vPortSVCHandler(void);
void xPortPendSVHandler(void);
void xPortSysTickHandler(void);

void NMI_Handler(void) __attribute__((weak, alias("Default_Handler")));
void HardFault_Handler(void) __attribute__((weak, alias("Default_Handler")));
void MemManage_Handler(void) __attribute__((weak, alias("Default_Handler")));
void BusFault_Handler(void) __attribute__((weak, alias("Default_Handler")));
void UsageFault_Handler(void) __attribute__((weak, alias("Default_Handler")));
void DebugMon_Handler(void) __attribute__((weak, alias("Default_Handler")));

__attribute__((section(".isr_vector")))
void (* const g_pfnVectors[])(void) = {
    (void (*)(void))(uintptr_t)&_estack,
    Reset_Handler,
    NMI_Handler,
    HardFault_Handler,
    MemManage_Handler,
    BusFault_Handler,
    UsageFault_Handler,
    0,
    0,
    0,
    0,
    vPortSVCHandler,
    DebugMon_Handler,
    0,
    xPortPendSVHandler,
    xPortSysTickHandler,
};

void Reset_Handler(void)
{
    uint32_t *src = &_sidata;
    uint32_t *dst = &_sdata;

    while (dst < &_edata) {
        *dst++ = *src++;
    }

    for (dst = &_sbss; dst < &_ebss;) {
        *dst++ = 0u;
    }

    (void)main();

    while (1) {
    }
}

void Default_Handler(void)
{
    while (1) {
    }
}
