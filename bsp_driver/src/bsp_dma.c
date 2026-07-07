/**
 * @file bsp_dma.c
 * @brief TM4C123 uDMA 初始化
 */

#include "bsp_dma.h"

#include "bsp_sysctl.h"

#include "bsp_config.h"

#include "driverlib/sysctl.h"
#include "driverlib/udma.h"

#define BSP_DMA_PERIPH_READY_US 100000U

static bool s_dma_ready;

bool bsp_dma_init(void)
{
    SysCtlPeripheralEnable(SYSCTL_PERIPH_UDMA);
    if (!bsp_periph_wait_ready(SYSCTL_PERIPH_UDMA, BSP_DMA_PERIPH_READY_US)) {
        s_dma_ready = false;
        return false;
    }

    uDMAEnable();
    s_dma_ready = true;
    return true;
}

bool bsp_dma_is_ready(void)
{
    return s_dma_ready;
}
