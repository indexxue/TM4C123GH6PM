/**
 * @file start.c
 * @brief 平台壳层初始化：按 device_profile 的 board_mask / platform_mask 拉起外设与服务。
 */

#include "start.h"

#include "device_profile.h"
#include "log.h"
#include "buzzer.h"

#include "board.h"

#include "bsp_sysctl.h"
#include "bsp_systick.h"
#include "bsp_uart.h"

static bsp_clock_source_t map_clock_source(device_clock_source_t source)
{
    switch (source) {
    case DEVICE_CLOCK_INT_PIOSC:
        return BSP_CLOCK_INT_PIOSC;
    case DEVICE_CLOCK_MAIN_8MHZ:
    default:
        return BSP_CLOCK_MAIN_8MHZ;
    }
}

void Start_Init(void)
{
    const device_product_profile_t *profile = device_profile_product();

    bsp_clock_init(map_clock_source(profile->clock_source));
    bsp_systick_init();

    /*
     * 尽早接管蜂鸣器并拉灭（避免上电到 App_Start 之间常响）。
     * car-2wd/car-4wd 同 PCB：蜂鸣器在 PB1，勿占用 PC0/SWCLK。
     */
#if (DEVICE_PRODUCT_ID != DEVICE_PRODUCT_ID_RC_CONTROLLER)
    buzzer_init();
#endif

    if (device_profile_platform_wants(DEVICE_PLATFORM_MASK_LOG)) {
        (void)Board_UartDebug_Init();
    }

#if (DEVICE_PRODUCT_ID != DEVICE_PRODUCT_ID_RC_CONTROLLER)
    if (device_profile_board_wants(DEVICE_BOARD_MASK_MOTOR)) {
        Motor_Init();
    }
#endif
    if (device_profile_board_wants(DEVICE_BOARD_MASK_PERIPH)) {
        if (!Board_Periph_Init()) {
            bsp_uart_debug_puts("[start] Board_Periph_Init FAILED\r\n");
        }
    }
#if (DEVICE_PRODUCT_ID != DEVICE_PRODUCT_ID_RC_CONTROLLER)
    if (device_profile_board_wants(DEVICE_BOARD_MASK_LINE)) {
        Line_Init();
    }
    if (device_profile_board_wants(DEVICE_BOARD_MASK_ENCODER)) {
        Encoder_Init();
    }
#endif

    if (device_profile_platform_wants(DEVICE_PLATFORM_MASK_LOG)) {
        if (log_init(NULL) != STATUS_OK) {
            bsp_uart_debug_puts("[start] log_init FAILED\r\n");
        } else {
            LOG_INFO("start: %s %luMHz", profile->name, (unsigned long)(bsp_clock_get_hz() / 1000000U));
        }
    }
}
