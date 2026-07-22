/**
 * @file    boot_slot.c
 * @brief   Boot NVS slot 切换（不依赖 UART cmd 框架）
 */

#include "boot_slot.h"

#include "board.h"
#include "bsp_sysctl.h"
#include "flash_layout.h"
#include "log.h"
#include "nvs.h"

static void boot_slot_motors_stop(void)
{
    uint8_t i;

    for (i = 1U; i <= BOARD_MOTOR_COUNT; i++) {
        Motor_SetSpeed(i, 0);
    }
}

status_t boot_slot_switch(uint32_t slot)
{
    uint32_t base;

    if (slot > BOOT_SLOT_B) {
        return STATUS_INVALID_ARG;
    }

    base = boot_slot_target_base(slot);
    if (!boot_image_is_valid(base)) {
        LOG_WARN("boot_slot: slot %lu image invalid @ 0x%08lX",
                 (unsigned long)slot, (unsigned long)base);
        return STATUS_FAIL;
    }

    if (nvs_boot_slot_set(slot) != STATUS_OK) {
        LOG_WARN("boot_slot: set slot %lu failed", (unsigned long)slot);
        return STATUS_FAIL;
    }

    LOG_INFO("boot_slot: slot=%lu reboot", (unsigned long)slot);
    boot_slot_motors_stop();
    bsp_system_reset();
    return STATUS_OK;
}
