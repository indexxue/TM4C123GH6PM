/**
 * @file    bootloader.c
 * @brief   TM4C123 Bootloader：校验 APP_A / APP_B 并按 NVS slot 跳转
 */

#include "bootloader.h"

#include "boot_slot.h"

#include <errno.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>

#include "flash_layout.h"

#include "inc/hw_memmap.h"
#include "inc/hw_types.h"
#include "driverlib/gpio.h"
#include "driverlib/pin_map.h"
#include "driverlib/sysctl.h"
#include "driverlib/uart.h"

#define BOOT_SCB_VTOR_ADDR  0xE000ED08U

/* -------------------------------------------------------------------------- */
/* newlib 存根                                                                 */
/* -------------------------------------------------------------------------- */

extern uint32_t _ebss;

#undef errno
extern int errno;

void *_sbrk(int incr)
{
    static uint32_t *heap_limit = (uint32_t *)&_ebss;
    uint32_t *prev = heap_limit;
    heap_limit += incr;
    return (void *)prev;
}

int _write(int file, char *ptr, int len)
{
    (void)file;
    (void)ptr;
    return len;
}

int _read(int file, char *ptr, int len)
{
    (void)file;
    (void)ptr;
    (void)len;
    return 0;
}

int _close(int file)
{
    (void)file;
    return -1;
}

int _fstat(int file, struct stat *st)
{
    (void)file;
    st->st_mode = S_IFCHR;
    return 0;
}

int _isatty(int file)
{
    (void)file;
    return 1;
}

int _lseek(int file, int ptr, int dir)
{
    (void)file;
    (void)ptr;
    (void)dir;
    return 0;
}

int _getpid(void)
{
    return 1;
}

int _kill(int pid, int sig)
{
    (void)pid;
    (void)sig;
    errno = EINVAL;
    return -1;
}

/* -------------------------------------------------------------------------- */
/* 启动与向量表                                                                 */
/* -------------------------------------------------------------------------- */

extern uint32_t _estack;
extern uint32_t _sidata;
extern uint32_t _sdata;
extern uint32_t _edata;
extern uint32_t _sbss;

int main(void);
void Reset_Handler(void);
void Default_Handler(void);

void NMI_Handler(void) __attribute__((weak, alias("Default_Handler")));
void HardFault_Handler(void) __attribute__((weak, alias("Default_Handler")));
void MemManage_Handler(void) __attribute__((weak, alias("Default_Handler")));
void BusFault_Handler(void) __attribute__((weak, alias("Default_Handler")));
void UsageFault_Handler(void) __attribute__((weak, alias("Default_Handler")));
void SVC_Handler(void) __attribute__((weak, alias("Default_Handler")));
void DebugMon_Handler(void) __attribute__((weak, alias("Default_Handler")));
void PendSV_Handler(void) __attribute__((weak, alias("Default_Handler")));
void SysTick_Handler(void) __attribute__((weak, alias("Default_Handler")));

__attribute__((section(".isr_vector")))
void (*const g_pfnVectors[])(void) = {
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
    SVC_Handler,
    DebugMon_Handler,
    0,
    PendSV_Handler,
    SysTick_Handler,
};

void Reset_Handler(void)
{
    uint32_t *src = &_sidata;
    uint32_t *dst = &_sdata;

    while (dst < &_edata) {
        *dst++ = *src++;
    }

    for (dst = &_sbss; dst < &_ebss;) {
        *dst++ = 0U;
    }

    (void)main();

    for (;;) {
    }
}

void Default_Handler(void)
{
    for (;;) {
    }
}

/* -------------------------------------------------------------------------- */
/* 硬件与调试输出                                                               */
/* -------------------------------------------------------------------------- */

static void boot_clock_init(void)
{
    SysCtlClockSet(SYSCTL_SYSDIV_5 | SYSCTL_USE_PLL |
                   SYSCTL_OSC_MAIN | SYSCTL_XTAL_16MHZ);
}

static void boot_debug_init(void)
{
    SysCtlPeripheralEnable(SYSCTL_PERIPH_UART7);
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOE);

    while (!SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOE)) {
    }

    GPIOPinConfigure(GPIO_PE1_U7TX);
    GPIOPinTypeUART(GPIO_PORTE_BASE, GPIO_PIN_1);

    UARTConfigSetExpClk(UART7_BASE, SysCtlClockGet(), 115200,
                        UART_CONFIG_WLEN_8 | UART_CONFIG_STOP_ONE | UART_CONFIG_PAR_NONE);
    UARTFIFOEnable(UART7_BASE);
    UARTEnable(UART7_BASE);
}

static void boot_hw_init(void)
{
    boot_clock_init();
    boot_debug_init();
}

static void boot_log_putc(char c)
{
    UARTCharPut(UART7_BASE, c);
}

static void boot_log_puts(const char *s)
{
    if (s == NULL) {
        return;
    }

    while (*s != '\0') {
        boot_log_putc(*s++);
    }
}

static void boot_log_hex32(uint32_t value)
{
    static const char hex[] = "0123456789ABCDEF";
    int shift;

    boot_log_puts("0x");
    for (shift = 28; shift >= 0; shift -= 4) {
        boot_log_putc(hex[(value >> (uint32_t)shift) & 0xFU]);
    }
}

/* -------------------------------------------------------------------------- */
/* APP 镜像校验与跳转                                                           */
/* -------------------------------------------------------------------------- */

static bool boot_app_range_ok(uint32_t app_base, uint32_t *app_end_out)
{
    if (app_base == FLASH_APP_A_BASE) {
        *app_end_out = FLASH_APP_A_END;
        return true;
    }
    if (app_base == FLASH_APP_B_BASE) {
        *app_end_out = FLASH_APP_B_END;
        return true;
    }
    return false;
}

bool boot_app_is_valid(uint32_t app_base)
{
    const uint32_t *vt = (const uint32_t *)app_base;
    uint32_t sp;
    uint32_t reset;
    uint32_t reset_addr;
    uint32_t app_end;

    if (!boot_app_range_ok(app_base, &app_end)) {
        return false;
    }

    sp = vt[0];
    reset = vt[1];

    if ((sp == 0xFFFFFFFFU) || (reset == 0xFFFFFFFFU)) {
        return false;
    }

    if ((sp <= 0x20000000U) || (sp > 0x20008000U)) {
        return false;
    }

    if ((reset & 1U) == 0U) {
        return false;
    }

    reset_addr = reset & ~1U;
    if ((reset_addr < app_base) || (reset_addr > app_end)) {
        return false;
    }

    return true;
}

void boot_app_jump(uint32_t app_base)
{
    uint32_t sp = *(volatile uint32_t *)app_base;
    void (*reset_handler)(void) =
        (void (*)(void))(*(volatile uint32_t *)(app_base + 4U));

    __asm volatile("cpsid i");

    HWREG(BOOT_SCB_VTOR_ADDR) = app_base;
    __asm volatile("msr msp, %0" : : "r"(sp) : );
    reset_handler();

    for (;;) {
    }
}

static uint32_t boot_pick_target(uint32_t preferred_slot)
{
    uint32_t preferred_base = boot_slot_target_base(preferred_slot);
    uint32_t fallback_base =
        (preferred_base == FLASH_APP_A_BASE) ? FLASH_APP_B_BASE : FLASH_APP_A_BASE;

    if (boot_app_is_valid(preferred_base)) {
        return preferred_base;
    }
    if (boot_app_is_valid(fallback_base)) {
        return fallback_base;
    }
    return 0U;
}

/* -------------------------------------------------------------------------- */
/* 入口                                                                        */
/* -------------------------------------------------------------------------- */

int main(void)
{
    uint32_t slot;
    uint32_t target;

    boot_hw_init();
    boot_log_puts("\r\n[boot] TM4C123 start\r\n");

    slot = boot_slot_read();
    target = boot_pick_target(slot);

    if (target == 0U) {
        boot_log_puts("[boot] APP_A/APP_B invalid, halt\r\n");
        for (;;) {
        }
    }

    boot_log_puts("[boot] slot=");
    boot_log_putc((slot == BOOT_SLOT_B) ? 'B' : 'A');
    boot_log_puts(" jump ");
    boot_log_hex32(target);
    boot_log_puts("\r\n");

    boot_app_jump(target);

    for (;;) {
    }
}
