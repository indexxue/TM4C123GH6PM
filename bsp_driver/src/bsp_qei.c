/**
 * @file bsp_qei.c
 * @brief TM4C123 QEI
 */

#include "bsp_qei.h"

#include "bsp_sysctl.h"

#include "bsp_config.h"

#include "driverlib/qei.h"
#include "driverlib/sysctl.h"
#include "inc/hw_memmap.h"

#define BSP_QEI_PERIPH_READY_US 100000U

bool bsp_qei_init(const bsp_qei_config_t *cfg)
{
    size_t i;

    if ((cfg == NULL) || (cfg->channels == NULL) || (cfg->channel_count == 0U)) {
        return false;
    }

    for (i = 0U; i < cfg->channel_count; i++) {
        const bsp_qei_channel_t *ch = &cfg->channels[i];

        SysCtlPeripheralEnable(ch->qei_periph);
        if (!bsp_periph_wait_ready(ch->qei_periph, BSP_QEI_PERIPH_READY_US)) {
            return false;
        }

        QEIConfigure(ch->qei_base,
                     QEI_CONFIG_CAPTURE_A_B | QEI_CONFIG_NO_RESET | QEI_CONFIG_QUADRATURE | QEI_CONFIG_NO_SWAP,
                     0);
        QEIEnable(ch->qei_base);
    }

    return true;
}

int32_t bsp_qei_get_position(uint32_t qei_base)
{
    return (int32_t)QEIPositionGet(qei_base);
}

void bsp_qei_reset(uint32_t qei_base)
{
    QEIPositionSet(qei_base, 0);
}
