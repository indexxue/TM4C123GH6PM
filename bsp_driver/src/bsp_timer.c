/**
 * @file bsp_timer.c
 * @brief TM4C123 Timer（含 PWM）
 */

#include "bsp_timer.h"

#include "bsp_sysctl.h"

#include "bsp_config.h"

#include "driverlib/sysctl.h"
#include "driverlib/timer.h"

#define BSP_PWM_PERIPH_READY_US 100000U
#define BSP_PWM_MIN_PERIOD      2U

bool bsp_pwm_init(const bsp_pwm_config_t *cfg)
{
    size_t i;
    uint32_t last_timer = 0U;

    if ((cfg == NULL) || (cfg->channels == NULL) || (cfg->channel_count == 0U) || (cfg->clock_hz == 0U)) {
        return false;
    }

    for (i = 0U; i < cfg->channel_count; i++) {
        const bsp_pwm_channel_t *ch = &cfg->channels[i];
        uint32_t period;

        if (ch->timer_base != last_timer) {
            SysCtlPeripheralEnable(ch->timer_periph);
            if (!bsp_periph_wait_ready(ch->timer_periph, BSP_PWM_PERIPH_READY_US)) {
                return false;
            }
            TimerConfigure(ch->timer_base, TIMER_CFG_SPLIT_PAIR | TIMER_CFG_A_PWM | TIMER_CFG_B_PWM);
            last_timer = ch->timer_base;
        }

        if (ch->frequency_hz == 0U) {
            continue;
        }

        if (ch->frequency_hz > cfg->clock_hz) {
            return false;
        }

        period = cfg->clock_hz / ch->frequency_hz;
        if (period < BSP_PWM_MIN_PERIOD) {
            return false;
        }

        TimerLoadSet(ch->timer_base, ch->channel, period);
        TimerMatchSet(ch->timer_base, ch->channel, period);
        TimerEnable(ch->timer_base, ch->channel);
    }

    return true;
}

void bsp_pwm_set_duty(uint32_t timer_base, uint32_t channel, uint16_t duty_permille)
{
    uint32_t load;
    uint32_t match;

    if (duty_permille > 1000U) {
        duty_permille = 1000U;
    }

    load = TimerLoadGet(timer_base, channel);
    if (load == 0U) {
        return;
    }

    match = (load * (1000U - duty_permille)) / 1000U;
    TimerMatchSet(timer_base, channel, match);
}
