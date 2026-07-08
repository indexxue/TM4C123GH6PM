/**
 * @file    bootloader.c
 * @brief   TM4C123 Bootloader：读 NVS slot，校验并跳转 APP_A / APP_B
 *
 * 时钟：8 MHz 主晶振 + PLL → 80 MHz（与 APP / BSP 一致）
 */

#include "bootloader.h"

#include <stdbool.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/stat.h>

#include "flash_layout.h"

#include "inc/hw_memmap.h"
#include "inc/hw_types.h"
#include "driverlib/gpio.h"
#include "driverlib/pin_map.h"
#include "driverlib/sysctl.h"
#include "driverlib/uart.h"

#define BOOT_SCB_VTOR_ADDR  0xE000ED08U
#define BOOT_UART_BAUD      115200U

/* newlib 存根（链接需要，无 printf） */
extern uint32_t _ebss;
#undef errno
extern int errno;

void *_sbrk(int incr)
{
    static uint32_t *heap = (uint32_t *)&_ebss;
    void *prev = heap;
    heap += incr;
    return prev;
}

int _write(int f, char *p, int n) { (void)f; (void)p; return n; }
int _read(int f, char *p, int n) { (void)f; (void)p; (void)n; return 0; }
int _close(int f) { (void)f; return -1; }
int _fstat(int f, struct stat *st) { (void)f; st->st_mode = S_IFCHR; return 0; }
int _isatty(int f) { (void)f; return 1; }
int _lseek(int f, int p, int d) { (void)f; (void)p; (void)d; return 0; }
int _getpid(void) { return 1; }
int _kill(int p, int s) { (void)p; (void)s; errno = EINVAL; return -1; }

extern uint32_t _estack, _sidata, _sdata, _edata, _sbss;

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
    NMI_Handler, HardFault_Handler, MemManage_Handler, BusFault_Handler, UsageFault_Handler,
    0, 0, 0, 0,
    SVC_Handler, DebugMon_Handler, 0, PendSV_Handler, SysTick_Handler,
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

static void boot_puts(const char *s)
{
    while ((s != NULL) && (*s != '\0')) {
        UARTCharPut(UART7_BASE, *s++);
    }
}

static void boot_uart_flush(void)
{
    while (UARTBusy(UART7_BASE)) {
    }
}

static void boot_log_target(uint32_t base, bool fallback)
{
    if (base == FLASH_APP_A_BASE) {
        boot_puts(fallback ? "boot A*\r\n" : "boot A\r\n");
    } else if (base == FLASH_APP_B_BASE) {
        boot_puts(fallback ? "boot B*\r\n" : "boot B\r\n");
    } else {
        boot_puts("boot ?\r\n");
    }
}

static void boot_hw_init(void)
{
    SysCtlClockSet(SYSCTL_SYSDIV_2_5 | SYSCTL_USE_PLL |
                   SYSCTL_OSC_MAIN | SYSCTL_XTAL_8MHZ);

    SysCtlPeripheralEnable(SYSCTL_PERIPH_UART7);
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOE);
    while (!SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOE)) {
    }
    while (!SysCtlPeripheralReady(SYSCTL_PERIPH_UART7)) {
    }

    GPIOPinConfigure(GPIO_PE1_U7TX);
    GPIOPinTypeUART(GPIO_PORTE_BASE, GPIO_PIN_1);
    UARTConfigSetExpClk(UART7_BASE, SysCtlClockGet(), BOOT_UART_BAUD,
                        UART_CONFIG_WLEN_8 | UART_CONFIG_STOP_ONE | UART_CONFIG_PAR_NONE);
    UARTFIFOEnable(UART7_BASE);
    UARTEnable(UART7_BASE);
}

static bool boot_pick_target(uint32_t *target_out, bool *fallback_out)
{
    uint32_t slot = boot_slot_read();
    uint32_t primary = boot_slot_target_base(slot);
    uint32_t alt = boot_slot_target_base(slot == BOOT_SLOT_A ? BOOT_SLOT_B : BOOT_SLOT_A);

    if (boot_image_is_valid(primary)) {
        *target_out = primary;
        *fallback_out = false;
        return true;
    }
    if (boot_image_is_valid(alt)) {
        *target_out = alt;
        *fallback_out = true;
        return true;
    }
    return false;
}

void boot_app_jump(uint32_t app_base)
{
    uint32_t sp;
    uint32_t reset;

    __asm volatile("dsb" ::: "memory");
    sp = *(volatile uint32_t *)app_base;
    reset = *(volatile uint32_t *)(app_base + 4U);

    __asm volatile("cpsid i");
    HWREG(BOOT_SCB_VTOR_ADDR) = app_base;
    __asm volatile("dsb" ::: "memory");
    __asm volatile("isb" ::: "memory");

    /* bx 使用向量表原值（含 Thumb 位），勿 & ~1U */
    __asm volatile(
        "msr msp, %0\n"
        "bx  %1\n"
        :
        : "r"(sp), "r"(reset)
        : "memory");

    for (;;) {
    }
}

int main(void)
{
    uint32_t target;
    bool fallback;

    boot_hw_init();

    if (!boot_pick_target(&target, &fallback)) {
        boot_puts("boot -\r\n");
        boot_uart_flush();
        for (;;) {
        }
    }

    boot_log_target(target, fallback);
    boot_uart_flush();
    boot_app_jump(target);

    for (;;) {
    }
}
