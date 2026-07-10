/**
 * @file bsp_qei.c
 * @brief TM4C123 硬件 QEI 正交解码（DriverLib 顺序见 qei.c / car-chassis-reference.md）
 */

#include "bsp_qei.h"

#include "bsp_sysctl.h"

#include "bsp_config.h"

#include "driverlib/qei.h"
#include "driverlib/sysctl.h"
#include "inc/hw_memmap.h"
#include "inc/hw_qei.h"
#include "inc/hw_types.h"

#define BSP_QEI_PERIPH_READY_US 100000U
/** TM4C123：QEIMAXPOS=0 时位置在下一正向边沿即回绕，读数恒为 0 */
#define BSP_QEI_MAX_POSITION      0xFFFFFFFFU

static bool qei_channel_init(const bsp_qei_channel_t *ch)
{
    if (ch == NULL) {
        return false;
    }

    SysCtlPeripheralEnable(ch->qei_periph);
    if (!bsp_periph_wait_ready(ch->qei_periph, BSP_QEI_PERIPH_READY_US)) {
        return false;
    }

    QEIDisable(ch->qei_base);
    QEIConfigure(ch->qei_base,
                 QEI_CONFIG_CAPTURE_A_B | QEI_CONFIG_NO_RESET | QEI_CONFIG_QUADRATURE | QEI_CONFIG_NO_SWAP,
                 BSP_QEI_MAX_POSITION);
    QEIFilterConfigure(ch->qei_base, QEI_FILTCNT_2);
    QEIFilterDisable(ch->qei_base);
    QEIPositionSet(ch->qei_base, 0U);
    QEIEnable(ch->qei_base);

    return (HWREG(ch->qei_base + QEI_O_CTL) & QEI_CTL_ENABLE) != 0U;
}

bool bsp_qei_init(const bsp_qei_config_t *cfg)
{
    size_t i;

    if ((cfg == NULL) || (cfg->channels == NULL) || (cfg->channel_count == 0U)) {
        return false;
    }

    for (i = 0U; i < cfg->channel_count; i++) {
        if (!qei_channel_init(&cfg->channels[i])) {
            return false;
        }
    }

    return true;
}

bool bsp_qei_is_enabled(uint32_t qei_base)
{
    return (HWREG(qei_base + QEI_O_CTL) & QEI_CTL_ENABLE) != 0U;
}

uint32_t bsp_qei_ctl_get(uint32_t qei_base)
{
    return HWREG(qei_base + QEI_O_CTL);
}

bool bsp_qei_has_error(uint32_t qei_base)
{
    return QEIErrorGet(qei_base);
}

int32_t bsp_qei_get_position(uint32_t qei_base)
{
    return (int32_t)QEIPositionGet(qei_base);
}

void bsp_qei_reset(uint32_t qei_base)
{
    QEIPositionSet(qei_base, 0U);
}
