/**
 * @file buzzer.c
 * @brief 蜂鸣器驱动（有源 GPIO / 无源 Timer PWM，由 BUZZER_TYPE 宏切换）
 */

#include "buzzer.h"

#include "board.h"

static bool s_initialized;

#if (BUZZER_TYPE == BUZZER_TYPE_ACTIVE)

#include "bsp_gpio.h"
#include "bsp_systick.h"

static const bsp_gpio_pin_t s_buzzer_pin = {
    .port_base = GPIO_BUZZER_PORT,
    .pin_mask = GPIO_BUZZER_PIN,
};

#if (GPIO_BUZZER_PORT == GPIO_PORTA_BASE)
#define BUZZER_GPIO_PORT_MASK (1u << 0)
#elif (GPIO_BUZZER_PORT == GPIO_PORTB_BASE)
#define BUZZER_GPIO_PORT_MASK (1u << 1)
#elif (GPIO_BUZZER_PORT == GPIO_PORTC_BASE)
#define BUZZER_GPIO_PORT_MASK (1u << 2)
#elif (GPIO_BUZZER_PORT == GPIO_PORTD_BASE)
#define BUZZER_GPIO_PORT_MASK (1u << 3)
#elif (GPIO_BUZZER_PORT == GPIO_PORTE_BASE)
#define BUZZER_GPIO_PORT_MASK (1u << 4)
#elif (GPIO_BUZZER_PORT == GPIO_PORTF_BASE)
#define BUZZER_GPIO_PORT_MASK (1u << 5)
#else
#error "buzzer.c: unsupported GPIO_BUZZER port"
#endif

static void buzzer_set_on(bool on)
{
#if BUZZER_ACTIVE_HIGH
    bsp_gpio_write(&s_buzzer_pin, on);
#else
    bsp_gpio_write(&s_buzzer_pin, !on);
#endif
}

void buzzer_init(void)
{
    if (s_initialized) {
        return;
    }

    (void)bsp_gpio_port_enable(BUZZER_GPIO_PORT_MASK);
    bsp_gpio_configure(&s_buzzer_pin, BSP_GPIO_DIR_OUTPUT, BSP_GPIO_PULL_NONE);
    buzzer_set_on(false);
    s_initialized = true;
}

void buzzer_start(uint32_t freq_hz, uint8_t duty_percent)
{
    (void)freq_hz;

    if (!s_initialized) {
        buzzer_init();
    }

    if (duty_percent == 0U) {
        buzzer_stop();
        return;
    }

    buzzer_set_on(true);
}

void buzzer_stop(void)
{
    if (!s_initialized) {
        return;
    }

    buzzer_set_on(false);
}

bool buzzer_beep(uint32_t freq_hz, uint32_t duration_ms)
{
    if (duration_ms == 0U) {
        return false;
    }

    buzzer_start(freq_hz, BUZZER_DEFAULT_DUTY_PERCENT);
    bsp_delay_ms(duration_ms);
    buzzer_stop();
    return true;
}

void buzzer_chirp(uint8_t count, uint32_t on_ms, uint32_t gap_ms)
{
    if (count == 0U) {
        return;
    }

    if (!s_initialized) {
        buzzer_init();
    }

    while (count-- > 0U) {
        (void)buzzer_beep(0U, on_ms);
        if ((count > 0U) && (gap_ms > 0U)) {
            bsp_delay_ms(gap_ms);
        }
    }

    buzzer_stop();
}

#else /* BUZZER_TYPE_PASSIVE */

#include "bsp_gpio.h"
#include "bsp_systick.h"
#include "bsp_sysctl.h"

#include "driverlib/gpio.h"
#include "driverlib/pin_map.h"
#include "driverlib/sysctl.h"
#include "driverlib/timer.h"
#include "inc/hw_memmap.h"

typedef struct {
    uint32_t timer_base;
    uint32_t timer_periph;
    uint32_t pwm_channel;
    uint32_t port_base;
    uint8_t pin_mask;
    uint32_t pin_mux;
    uint32_t port_enable_mask;
} buzzer_hw_t;

#if (GPIO_BUZZER_PORT == GPIO_PORTB_BASE) && (GPIO_BUZZER_PIN == GPIO_PIN_1)
static const buzzer_hw_t s_buzzer_hw = {
    .timer_base = TIMER2_BASE,
    .timer_periph = SYSCTL_PERIPH_TIMER2,
    .pwm_channel = TIMER_B,
    .port_base = GPIO_PORTB_BASE,
    .pin_mask = GPIO_PIN_1,
    .pin_mux = GPIO_PB1_T2CCP1,
    .port_enable_mask = (1u << 1),
};
#elif (GPIO_BUZZER_PORT == GPIO_PORTC_BASE) && (GPIO_BUZZER_PIN == GPIO_PIN_0)
static const buzzer_hw_t s_buzzer_hw = {
    .timer_base = TIMER4_BASE,
    .timer_periph = SYSCTL_PERIPH_TIMER4,
    .pwm_channel = TIMER_A,
    .port_base = GPIO_PORTC_BASE,
    .pin_mask = GPIO_PIN_0,
    .pin_mux = GPIO_PC0_T4CCP0,
    .port_enable_mask = (1u << 2),
};
#else
#error "buzzer.c: unsupported GPIO_BUZZER pin — add Timer CCP mapping"
#endif

static uint32_t s_running_freq_hz;

static bool buzzer_calc_period(uint32_t freq_hz, uint32_t *period)
{
    uint32_t clock_hz = bsp_clock_get_hz();

    if ((freq_hz < BUZZER_FREQ_MIN_HZ) || (freq_hz > BUZZER_FREQ_MAX_HZ) || (clock_hz == 0U)) {
        return false;
    }

    *period = clock_hz / freq_hz;
    if (*period < 2U) {
        return false;
    }

    return true;
}

static bool buzzer_apply_pwm(uint32_t freq_hz, uint8_t duty_percent)
{
    uint32_t period;
    uint32_t match;

    if (!buzzer_calc_period(freq_hz, &period)) {
        return false;
    }

    if (duty_percent > 100U) {
        duty_percent = 100U;
    }

    TimerLoadSet(s_buzzer_hw.timer_base, s_buzzer_hw.pwm_channel, period);
    match = (period * (uint32_t)(100U - duty_percent)) / 100U;
    TimerMatchSet(s_buzzer_hw.timer_base, s_buzzer_hw.pwm_channel, match);
    s_running_freq_hz = freq_hz;
    return true;
}

void buzzer_init(void)
{
    uint32_t timer_cfg;

    if (s_initialized) {
        return;
    }

    (void)bsp_gpio_port_enable(s_buzzer_hw.port_enable_mask);
    SysCtlPeripheralEnable(s_buzzer_hw.timer_periph);
    (void)bsp_periph_wait_ready(s_buzzer_hw.timer_periph, 100000U);

    GPIOPinConfigure(s_buzzer_hw.pin_mux);
    GPIOPinTypeTimer(s_buzzer_hw.port_base, s_buzzer_hw.pin_mask);

    timer_cfg = TIMER_CFG_SPLIT_PAIR;
    timer_cfg |= (s_buzzer_hw.pwm_channel == TIMER_A) ? TIMER_CFG_A_PWM : TIMER_CFG_B_PWM;
    TimerConfigure(s_buzzer_hw.timer_base, timer_cfg);

    (void)buzzer_apply_pwm(BUZZER_DEFAULT_FREQ_HZ, 0U);
    TimerEnable(s_buzzer_hw.timer_base, s_buzzer_hw.pwm_channel);

    s_initialized = true;
}

void buzzer_start(uint32_t freq_hz, uint8_t duty_percent)
{
    if (!s_initialized) {
        buzzer_init();
    }

    if (duty_percent == 0U) {
        buzzer_stop();
        return;
    }

    if (freq_hz == 0U) {
        freq_hz = (s_running_freq_hz != 0U) ? s_running_freq_hz : BUZZER_DEFAULT_FREQ_HZ;
    }

    (void)buzzer_apply_pwm(freq_hz, duty_percent);
}

void buzzer_stop(void)
{
    uint32_t freq_hz;

    if (!s_initialized) {
        return;
    }

    freq_hz = (s_running_freq_hz != 0U) ? s_running_freq_hz : BUZZER_DEFAULT_FREQ_HZ;
    (void)buzzer_apply_pwm(freq_hz, 0U);
}

bool buzzer_beep(uint32_t freq_hz, uint32_t duration_ms)
{
    if (duration_ms == 0U) {
        return false;
    }

    if (freq_hz == 0U) {
        freq_hz = BUZZER_DEFAULT_FREQ_HZ;
    }

    buzzer_start(freq_hz, BUZZER_DEFAULT_DUTY_PERCENT);
    bsp_delay_ms(duration_ms);
    buzzer_stop();
    return true;
}

void buzzer_chirp(uint8_t count, uint32_t on_ms, uint32_t gap_ms)
{
    if (count == 0U) {
        return;
    }

    if (!s_initialized) {
        buzzer_init();
    }

    while (count-- > 0U) {
        (void)buzzer_beep(BUZZER_DEFAULT_FREQ_HZ, on_ms);
        if ((count > 0U) && (gap_ms > 0U)) {
            bsp_delay_ms(gap_ms);
        }
    }

    buzzer_stop();
}

#endif /* BUZZER_TYPE */
