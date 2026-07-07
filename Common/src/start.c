/**
 * @file start.c
 * @brief 平台壳层初始化：按 device_profile 的 board_mask / platform_mask 拉起外设与服务。
 */

#include "start.h"

#include "device_profile.h"
#include "log.h"

#include "board.h"

#include "bsp_sysctl.h"
#include "bsp_systick.h"
#include "bsp_uart.h"

static bsp_clock_source_t map_clock_source(device_clock_source_t source)
{
    switch (source) {
    case DEVICE_CLOCK_INT_PIOSC:
        return BSP_CLOCK_INT_PIOSC;
    case DEVICE_CLOCK_MAIN_16MHZ:
        return BSP_CLOCK_MAIN_16MHZ;
    case DEVICE_CLOCK_MAIN_8MHZ:
    default:
        return BSP_CLOCK_MAIN_8MHZ;
    }
}

static void start_put_u32(uint32_t value)
{
    char buf[11];
    int i = 10;

    buf[i] = '\0';
    if (value == 0U) {
        bsp_uart_debug_putc('0');
        return;
    }

    while ((value > 0U) && (i > 0)) {
        buf[--i] = (char)('0' + (value % 10U));
        value /= 10U;
    }
    bsp_uart_debug_puts(&buf[i]);
}

static void start_report_clock_hz(void)
{
    bsp_uart_debug_puts("[start] clock=");
    start_put_u32(bsp_clock_get_hz());
    bsp_uart_debug_puts(" Hz\r\n");
}

void Start_Init(void)
{
    const device_product_profile_t *profile = device_profile_product();

    bsp_clock_init(map_clock_source(profile->clock_source));
    bsp_systick_init();

    if (device_profile_platform_wants(DEVICE_PLATFORM_MASK_LOG)) {
        if (Board_UartDebug_Init()) {
            bsp_uart_debug_puts("\r\n[start] UART7 PE1 ready @ 115200\r\n");
        }
    }

    if (device_profile_board_wants(DEVICE_BOARD_MASK_MOTOR)) {
        Motor_Init();
    }
    if (device_profile_board_wants(DEVICE_BOARD_MASK_ENCODER)) {
        Encoder_Init();
    }
    if (device_profile_board_wants(DEVICE_BOARD_MASK_LINE)) {
        Line_Init();
    }
    if (device_profile_board_wants(DEVICE_BOARD_MASK_PERIPH)) {
        if (!Board_Periph_Init()) {
            bsp_uart_debug_puts("[start] Board_Periph_Init FAILED\r\n");
        }
    }

    if (device_profile_platform_wants(DEVICE_PLATFORM_MASK_LOG)) {
        if (log_init(NULL) != STATUS_OK) {
            bsp_uart_debug_puts("[start] log_init FAILED\r\n");
        } else {
            bsp_uart_debug_puts("[start] log_init OK\r\n");
            start_report_clock_hz();
            LOG_INFO("start: %s booting", profile->name);
        }
    }
}
