/**
 * @file    nvs_flash_ops.c
 * @brief   TivaWare FlashErase / FlashProgram 封装，禁止误擦 BL / APP_A
 */

#include "nvs_flash_ops.h"

#include <stdbool.h>
#include <stdint.h>

#include "flash_layout.h"

#include "driverlib/flash.h"
#include "driverlib/interrupt.h"

static status_t nvs_flash_check_range(uint32_t address, uint32_t length, nvs_flash_allow_t allow)
{
    uint32_t end;

    if (length == 0U) {
        return STATUS_INVALID_ARG;
    }

    if ((address % FLASH_PROGRAM_ALIGN) != 0U) {
        return STATUS_INVALID_ARG;
    }

    if ((length % FLASH_PROGRAM_ALIGN) != 0U) {
        return STATUS_INVALID_ARG;
    }

    end = address + length - 1U;
    if (end < address) {
        return STATUS_INVALID_ARG;
    }

    if (flash_addr_in_range(address, FLASH_BL_BASE, FLASH_BL_SIZE) ||
        flash_addr_in_range(end, FLASH_BL_BASE, FLASH_BL_SIZE)) {
        return STATUS_INVALID_ARG;
    }

    if (flash_addr_in_range(address, FLASH_APP_A_BASE, FLASH_APP_A_SIZE) ||
        flash_addr_in_range(end, FLASH_APP_A_BASE, FLASH_APP_A_SIZE)) {
        return STATUS_INVALID_ARG;
    }

    if ((allow & NVS_FLASH_ALLOW_NVS) != 0U) {
        if (flash_addr_in_range(address, FLASH_NVS_BASE, FLASH_NVS_SIZE) &&
            flash_addr_in_range(end, FLASH_NVS_BASE, FLASH_NVS_SIZE)) {
            return STATUS_OK;
        }
    }

    if ((allow & NVS_FLASH_ALLOW_STAGING) != 0U) {
        if (flash_addr_in_range(address, FLASH_STAGING_BASE, FLASH_STAGING_SIZE) &&
            flash_addr_in_range(end, FLASH_STAGING_BASE, FLASH_STAGING_SIZE)) {
            return STATUS_OK;
        }
    }

    return STATUS_INVALID_ARG;
}

status_t nvs_flash_erase(uint32_t address, uint32_t length, nvs_flash_allow_t allow)
{
    uint32_t offset;
    int32_t rc;

    if (nvs_flash_check_range(address, length, allow) != STATUS_OK) {
        return STATUS_INVALID_ARG;
    }

    for (offset = 0U; offset < length; offset += FLASH_ERASE_BLOCK_SIZE) {
        uint32_t block_addr = address + offset;

        IntMasterDisable();
        rc = FlashErase(block_addr);
        IntMasterEnable();

        if (rc != 0) {
            return STATUS_FAIL;
        }
    }

    return STATUS_OK;
}

status_t nvs_flash_program(uint32_t address, const void *data, uint32_t length,
                           nvs_flash_allow_t allow)
{
    int32_t rc;
    const uint32_t *words;

    if ((data == NULL) || (length == 0U)) {
        return STATUS_INVALID_ARG;
    }

    if (nvs_flash_check_range(address, length, allow) != STATUS_OK) {
        return STATUS_INVALID_ARG;
    }

    words = (const uint32_t *)data;

    IntMasterDisable();
    rc = FlashProgram((uint32_t *)words, address, length / sizeof(uint32_t));
    IntMasterEnable();

    return (rc == 0) ? STATUS_OK : STATUS_FAIL;
}
