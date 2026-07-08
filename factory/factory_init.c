/**
 * @file    factory_init.c
 * @brief   厂测固件板级与外设初始化
 */

#include "factory.h"

#include "board.h"
#include "bsp_sysctl.h"
#include "button.h"
#include "log.h"
#include "cmd.h"
#include "nvs.h"
#include "flash_layout.h"

void Factory_Board_Init(void)
{
    bsp_clock_init(BSP_CLOCK_MAIN_8MHZ);
    Motor_Init();
    Encoder_Init();
    Line_Init();
    Board_Periph_Init();

    (void)log_init(NULL);

    if (nvs_init() != STATUS_OK) {
        LOG_WARN("factory: nvs_init failed");
    } else {
        (void)nvs_startup_finalize();
    }

    (void)cmd_uart_line_service_start();

    button_init(factory_button_notify);

    LOG_INFO("factory: init @ 0x%08lX (APP_B storage)",
             (unsigned long)FLASH_APP_B_BASE);
}
