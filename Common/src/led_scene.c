/**
 * @file    led_scene.c
 * @brief   LED 场景驱动（WS2812B @ PC3 / board.h RGB_LED）
 */

#include "led_scene.h"

#include "board.h"
#include "log.h"
#include "ws2812b.h"

#include "bsp_gpio.h"
#include "bsp_systick.h"

#include "driverlib/gpio.h"
#include "driverlib/interrupt.h"

#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"

#include "inc/hw_types.h"

#include <stddef.h>
#include <string.h>

#define LED_SCENE_PIXEL_COUNT       1U
#define LED_SCENE_BOOTUP_MS         10000U
#define LED_SCENE_RAINBOW_BRIGHT    160U

#define DWT_CYCCNT                  (*(volatile uint32_t *)0xE0001004u)

/*
 * WS2812 位时序 @ 80 MHz（按逻辑分析仪实测校准）
 * 实测 T0H≈0.70µs 时 WS2812 易把 0 判成 1 → 灯珠只锁第一帧、看起来不变。
 * 目标：T0H≈0.40µs，T1H≈0.75µs（固定 GPIO 写开销约 0.30µs）
 */
#define WS2812_T0H_CYCLES           10U
#define WS2812_T0L_CYCLES           56U
#define WS2812_T1H_CYCLES           36U
#define WS2812_T1L_CYCLES           44U

#if (GPIO_RGB_LED_PORT == GPIO_PORTA_BASE)
#define LED_GPIO_PORT_MASK (1u << 0)
#elif (GPIO_RGB_LED_PORT == GPIO_PORTB_BASE)
#define LED_GPIO_PORT_MASK (1u << 1)
#elif (GPIO_RGB_LED_PORT == GPIO_PORTC_BASE)
#define LED_GPIO_PORT_MASK (1u << 2)
#elif (GPIO_RGB_LED_PORT == GPIO_PORTD_BASE)
#define LED_GPIO_PORT_MASK (1u << 3)
#elif (GPIO_RGB_LED_PORT == GPIO_PORTE_BASE)
#define LED_GPIO_PORT_MASK (1u << 4)
#elif (GPIO_RGB_LED_PORT == GPIO_PORTF_BASE)
#define LED_GPIO_PORT_MASK (1u << 5)
#else
#error "led_scene.c: unsupported GPIO_RGB_LED port"
#endif

typedef struct
{
    led_scene_id_e id;
    bool running;
    uint8_t current_cycle;
    uint8_t current_action;
    uint8_t action_cycle;
    uint32_t action_start_ms;
    led_rgb_value_t current_rgb;
} led_scene_state_t;

typedef struct
{
    led_scene_state_t states[LED_SCENE_ID_MAX_NUM];
    led_scene_id_e active_scene;
    bool initialized;
} led_scene_self_t;

static led_scene_self_t self;
static ws2812b_t s_ws2812;
static uint8_t s_ws2812_pixels[LED_SCENE_PIXEL_COUNT * 3U];
static TimerHandle_t s_onoff_timer;
static volatile uint32_t *const s_ws2812_data =
    (volatile uint32_t *)(GPIO_RGB_LED_PORT + (GPIO_RGB_LED_PIN << 2));

static void led_scene_begin_current_action(void);
static void led_scene_advance_action(void);
static void led_scene_finish_active_scene(void);

static const led_scene_t led_scene_bootup =
{
    .cycle = 1U,
    .num = 1U,
    .action[0] =
    {
        .cycle = 1U,
        .type = ACTION_ONOFF,
        .sub.onoff.value.r = 0xFFU,
        .sub.onoff.value.g = 0U,
        .sub.onoff.value.b = 0U,
        .sub.onoff.lifetime = LED_SCENE_BOOTUP_MS * LED_SCENE_MSEC,
    },
};

static const led_scene_t led_scene_pairing =
{
    .cycle = 30U,
    .num = 2U,
    .action[0] =
    {
        .cycle = 1U,
        .type = ACTION_ONOFF,
        .sub.onoff.value.r = 0U,
        .sub.onoff.value.g = 0U,
        .sub.onoff.value.b = 0xFFU,
        .sub.onoff.lifetime = 1000U * LED_SCENE_MSEC,
    },
    .action[1] =
    {
        .cycle = 1U,
        .type = ACTION_ONOFF,
        .sub.onoff.value.r = 0U,
        .sub.onoff.value.g = 0U,
        .sub.onoff.value.b = 0U,
        .sub.onoff.lifetime = 1000U * LED_SCENE_MSEC,
    },
};

static const led_scene_t led_scene_trigger =
{
    .cycle = 10U,
    .num = 2U,
    .action[0] =
    {
        .cycle = 1U,
        .type = ACTION_ONOFF,
        .sub.onoff.value.r = 0U,
        .sub.onoff.value.g = 0U,
        .sub.onoff.value.b = 0U,
        .sub.onoff.lifetime = 500U * LED_SCENE_MSEC,
    },
    .action[1] =
    {
        .cycle = 1U,
        .type = ACTION_ONOFF,
        .sub.onoff.value.r = 0xFFU,
        .sub.onoff.value.g = 0U,
        .sub.onoff.value.b = 0U,
        .sub.onoff.lifetime = 500U * LED_SCENE_MSEC,
    },
};

static const led_scene_t led_scene_error =
{
    .cycle = 10U,
    .num = 2U,
    .action[0] =
    {
        .cycle = 1U,
        .type = ACTION_ONOFF,
        .sub.onoff.value.r = 0xFFU,
        .sub.onoff.value.g = 0U,
        .sub.onoff.value.b = 0U,
        .sub.onoff.lifetime = 100U * LED_SCENE_MSEC,
    },
    .action[1] =
    {
        .cycle = 1U,
        .type = ACTION_ONOFF,
        .sub.onoff.value.r = 0U,
        .sub.onoff.value.g = 0U,
        .sub.onoff.value.b = 0U,
        .sub.onoff.lifetime = 100U * LED_SCENE_MSEC,
    },
};

static const led_scene_t led_scene_success =
{
    .cycle = 20U,
    .num = 2U,
    .action[0] =
    {
        .cycle = 1U,
        .type = ACTION_ONOFF,
        .sub.onoff.value.r = 0U,
        .sub.onoff.value.g = 0xFFU,
        .sub.onoff.value.b = 0U,
        .sub.onoff.lifetime = 500U * LED_SCENE_MSEC,
    },
    .action[1] =
    {
        .cycle = 1U,
        .type = ACTION_ONOFF,
        .sub.onoff.value.r = 0xFFU,
        .sub.onoff.value.g = 0U,
        .sub.onoff.value.b = 0U,
        .sub.onoff.lifetime = 500U * LED_SCENE_MSEC,
    },
};

static const led_scene_tab_t scene_table[LED_SCENE_ID_MAX_NUM] =
{
    [LED_SCENE_ID_BOOTUP]  = {.prio = LED_SCENE_PRIO_LOW,    .scene = &led_scene_bootup},
    [LED_SCENE_ID_PAIRING] = {.prio = LED_SCENE_PRIO_PAIR,   .scene = &led_scene_pairing},
    [LED_SCENE_ID_TRIGGER] = {.prio = LED_SCENE_PRIO_NORMAL, .scene = &led_scene_trigger},
    [LED_SCENE_ID_ERROR]   = {.prio = LED_SCENE_PRIO_NORMAL, .scene = &led_scene_error},
    [LED_SCENE_ID_SUCCESS] = {.prio = LED_SCENE_PRIO_NORMAL, .scene = &led_scene_success},
};

static void ws2812_delay_cycles(uint32_t cycles)
{
    uint32_t start = DWT_CYCCNT;

    while ((DWT_CYCCNT - start) < cycles) {
    }
}

static void ws2812_send_bit(bool one)
{
    if (one) {
        *s_ws2812_data = GPIO_RGB_LED_PIN;
        ws2812_delay_cycles(WS2812_T1H_CYCLES);
        *s_ws2812_data = 0U;
        ws2812_delay_cycles(WS2812_T1L_CYCLES);
    } else {
        *s_ws2812_data = GPIO_RGB_LED_PIN;
        ws2812_delay_cycles(WS2812_T0H_CYCLES);
        *s_ws2812_data = 0U;
        ws2812_delay_cycles(WS2812_T0L_CYCLES);
    }
}

static void ws2812_send_byte(uint8_t byte)
{
    for (int bit = 7; bit >= 0; bit--) {
        ws2812_send_bit((byte & (1U << (uint32_t)bit)) != 0U);
    }
}

static int ws2812_pc3_transmit(const uint8_t *grb, size_t len, void *ctx)
{
    bool ints_were_disabled;

    (void)ctx;

    if ((grb == NULL) || (len == 0U)) {
        return -1;
    }

    if (!bsp_dwt_is_ready()) {
        return -1;
    }

    /*
     * Camera SSI ISR 优先级 0x40，高于 configMAX_SYSCALL(0xA0)。
     * taskENTER_CRITICAL 挡不住该 ISR，会打断 ~µs 级位时序 → 灯效失效。
     * 整帧约数十 µs，关总中断即可；SSI 下一拍仍会补 FIFO。
     */
    ints_were_disabled = IntMasterDisable();
    for (size_t i = 0U; i < len; i++) {
        ws2812_send_byte(grb[i]);
    }
    if (!ints_were_disabled) {
        IntMasterEnable();
    }

    return 0;
}

static uint32_t led_scene_now_ms(void)
{
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

static led_rgb_value_t led_scene_hsv_to_rgb(uint8_t hue)
{
    led_rgb_value_t rgb = {0U, 0U, 0U};
    uint8_t region = hue / 43U;
    uint8_t rem = (uint8_t)((hue - (region * 43U)) * 6U);
    uint8_t p = 0U;
    uint8_t q = (uint8_t)((LED_SCENE_RAINBOW_BRIGHT * (255U - rem)) / 255U);
    uint8_t t = (uint8_t)((LED_SCENE_RAINBOW_BRIGHT * rem) / 255U);

    switch (region) {
    case 0U:
        rgb.r = LED_SCENE_RAINBOW_BRIGHT;
        rgb.g = t;
        rgb.b = p;
        break;
    case 1U:
        rgb.r = q;
        rgb.g = LED_SCENE_RAINBOW_BRIGHT;
        rgb.b = p;
        break;
    case 2U:
        rgb.r = p;
        rgb.g = LED_SCENE_RAINBOW_BRIGHT;
        rgb.b = t;
        break;
    case 3U:
        rgb.r = p;
        rgb.g = q;
        rgb.b = LED_SCENE_RAINBOW_BRIGHT;
        break;
    case 4U:
        rgb.r = t;
        rgb.g = p;
        rgb.b = LED_SCENE_RAINBOW_BRIGHT;
        break;
    default:
        rgb.r = LED_SCENE_RAINBOW_BRIGHT;
        rgb.g = p;
        rgb.b = q;
        break;
    }

    return rgb;
}

static void led_scene_output(const led_rgb_value_t *rgb)
{
    if (!ws2812b_is_initialized(&s_ws2812)) {
        return;
    }

    if (rgb == NULL) {
        (void)ws2812b_clear(&s_ws2812);
        return;
    }

    (void)ws2812b_set_pixel_rgb(&s_ws2812, 0U, rgb->r, rgb->g, rgb->b);
    (void)ws2812b_refresh(&s_ws2812);
}

static bool led_scene_any_running(void)
{
    for (uint8_t i = 0U; i < (uint8_t)LED_SCENE_ID_MAX_NUM; i++) {
        if (self.states[i].running) {
            return true;
        }
    }

    return false;
}

static void led_scene_bootup_rainbow_update(const led_scene_state_t *state)
{
    uint32_t elapsed_ms = led_scene_now_ms() - state->action_start_ms;
    uint8_t hue;

    if (elapsed_ms >= LED_SCENE_BOOTUP_MS) {
        return;
    }

    hue = (uint8_t)((elapsed_ms * 256UL) / LED_SCENE_BOOTUP_MS);
    {
        led_rgb_value_t rgb = led_scene_hsv_to_rgb(hue);
        led_scene_output(&rgb);
    }
}

static led_scene_id_e led_scene_find_highest_priority(void)
{
    led_scene_id_e highest_id = LED_SCENE_ID_MAX_NUM;
    led_scene_prio_e highest_prio = LED_SCENE_PRIO_MAX_NUM;

    for (uint8_t i = 0U; i < (uint8_t)LED_SCENE_ID_MAX_NUM; i++) {
        if (self.states[i].running) {
            led_scene_prio_e prio = scene_table[i].prio;
            if (prio < highest_prio) {
                highest_prio = prio;
                highest_id = (led_scene_id_e)i;
            }
        }
    }

    return highest_id;
}

static void led_scene_reset_active_state(led_scene_state_t *state, const led_scene_t *scene)
{
    if ((state == NULL) || (scene == NULL)) {
        return;
    }

    state->current_cycle = 0U;
    state->current_action = 0U;
    state->action_cycle = 0U;
    state->action_start_ms = led_scene_now_ms();
    state->current_rgb.r = 0U;
    state->current_rgb.g = 0U;
    state->current_rgb.b = 0U;

    if (scene->num > 0U) {
        const led_scene_action_t *action = &scene->action[0];
        if (action->type == ACTION_FADE) {
            state->current_rgb = action->sub.fade.start_value;
        }
    }
}

static void led_scene_timer_stop(void)
{
    if ((s_onoff_timer != NULL) && (xTimerIsTimerActive(s_onoff_timer) == pdTRUE)) {
        (void)xTimerStop(s_onoff_timer, 0);
    }
}

static bool led_scene_timer_start(uint32_t period_ms)
{
    TickType_t ticks = pdMS_TO_TICKS(period_ms);

    if (s_onoff_timer == NULL) {
        return false;
    }
    if (ticks == 0U) {
        ticks = 1U;
    }
    if (xTimerChangePeriod(s_onoff_timer, ticks, 0) != pdPASS) {
        return false;
    }
    return xTimerStart(s_onoff_timer, 0) == pdPASS;
}

static void led_scene_onoff_timer_cb(TimerHandle_t timer)
{
    (void)timer;
    led_scene_advance_action();
}

static void led_scene_begin_current_action(void)
{
    if (!self.initialized || (self.active_scene >= LED_SCENE_ID_MAX_NUM)) {
        return;
    }

    led_scene_state_t *state = &self.states[self.active_scene];
    const led_scene_t *scene = scene_table[self.active_scene].scene;

    if (!state->running || (scene == NULL) || (state->current_action >= scene->num)) {
        return;
    }

    const led_scene_action_t *action = &scene->action[state->current_action];
    state->action_start_ms = led_scene_now_ms();

    if (action->type == ACTION_ONOFF) {
        if (self.active_scene == LED_SCENE_ID_BOOTUP) {
            led_scene_bootup_rainbow_update(state);
            LOG_INFO("led_scene: bootup rainbow %lums", (unsigned long)LED_SCENE_BOOTUP_MS);
        } else {
            led_scene_output(&action->sub.onoff.value);
        }

        if (!led_scene_timer_start(action->sub.onoff.lifetime)) {
            LOG_WARN("led_scene: onoff timer start failed");
        }
        return;
    }

    state->current_rgb = action->sub.fade.start_value;
    led_scene_output(&state->current_rgb);
}

static void led_scene_finish_active_scene(void)
{
    led_scene_id_e done_id;

    if (!self.initialized || (self.active_scene >= LED_SCENE_ID_MAX_NUM)) {
        return;
    }

    led_scene_state_t *state = &self.states[self.active_scene];

    if (!state->running) {
        return;
    }

    done_id = self.active_scene;
    state->running = false;
    led_scene_timer_stop();
    led_scene_output(NULL);
    LOG_INFO("led_scene: scene %u done", (unsigned)done_id);

    self.active_scene = led_scene_find_highest_priority();
    if (self.active_scene >= LED_SCENE_ID_MAX_NUM) {
        return;
    }

    state = &self.states[self.active_scene];
    led_scene_reset_active_state(state, scene_table[self.active_scene].scene);
    led_scene_begin_current_action();
}

static void led_scene_advance_action(void)
{
    if (!self.initialized || (self.active_scene >= LED_SCENE_ID_MAX_NUM)) {
        return;
    }

    led_scene_state_t *state = &self.states[self.active_scene];
    const led_scene_t *scene = scene_table[self.active_scene].scene;

    if (!state->running || (scene == NULL)) {
        return;
    }

    const led_scene_action_t *completed_action = &scene->action[state->current_action];

    state->action_cycle++;
    if (state->action_cycle >= completed_action->cycle) {
        state->action_cycle = 0U;
        state->current_action++;
        if (state->current_action >= scene->num) {
            state->current_action = 0U;
            state->current_cycle++;
            if ((scene->cycle != CYCLE_ALWAYS) && (state->current_cycle >= scene->cycle)) {
                led_scene_finish_active_scene();
                return;
            }
        }
    }

    if (completed_action->type == ACTION_FADE) {
        state->current_rgb = completed_action->sub.fade.start_value;
    }
    led_scene_begin_current_action();
}

void led_scene_update(void)
{
    if (!self.initialized) {
        return;
    }

    if (self.active_scene >= LED_SCENE_ID_MAX_NUM) {
        return;
    }

    led_scene_state_t *state = &self.states[self.active_scene];
    const led_scene_t *scene = scene_table[self.active_scene].scene;

    if (!state->running || (scene == NULL)) {
        return;
    }

    const led_scene_action_t *action = &scene->action[state->current_action];

    if (action->type == ACTION_ONOFF) {
        if (self.active_scene == LED_SCENE_ID_BOOTUP) {
            uint32_t elapsed_ms = led_scene_now_ms() - state->action_start_ms;

            if (elapsed_ms >= LED_SCENE_BOOTUP_MS) {
                led_scene_finish_active_scene();
            } else {
                led_scene_bootup_rainbow_update(state);
            }
        }
        return;
    }

    bool action_complete = false;
    uint32_t elapsed_ms = led_scene_now_ms() - state->action_start_ms;

    if (action->type == ACTION_FADE) {
        if (elapsed_ms >= action->sub.fade.interval) {
            bool fade_complete = false;

            if (action->sub.fade.start_value.r < action->sub.fade.end_value.r) {
                if (state->current_rgb.r + action->sub.fade.step >= action->sub.fade.end_value.r) {
                    state->current_rgb.r = action->sub.fade.end_value.r;
                    fade_complete = true;
                } else {
                    state->current_rgb.r += action->sub.fade.step;
                }
            } else if (action->sub.fade.start_value.r > action->sub.fade.end_value.r) {
                if (state->current_rgb.r <= action->sub.fade.step ||
                    state->current_rgb.r - action->sub.fade.step <= action->sub.fade.end_value.r) {
                    state->current_rgb.r = action->sub.fade.end_value.r;
                    fade_complete = true;
                } else {
                    state->current_rgb.r -= action->sub.fade.step;
                }
            }

            if (action->sub.fade.start_value.g < action->sub.fade.end_value.g) {
                if (state->current_rgb.g + action->sub.fade.step >= action->sub.fade.end_value.g) {
                    state->current_rgb.g = action->sub.fade.end_value.g;
                    fade_complete = true;
                } else {
                    state->current_rgb.g += action->sub.fade.step;
                }
            } else if (action->sub.fade.start_value.g > action->sub.fade.end_value.g) {
                if (state->current_rgb.g <= action->sub.fade.step ||
                    state->current_rgb.g - action->sub.fade.step <= action->sub.fade.end_value.g) {
                    state->current_rgb.g = action->sub.fade.end_value.g;
                    fade_complete = true;
                } else {
                    state->current_rgb.g -= action->sub.fade.step;
                }
            }

            if (action->sub.fade.start_value.b < action->sub.fade.end_value.b) {
                if (state->current_rgb.b + action->sub.fade.step >= action->sub.fade.end_value.b) {
                    state->current_rgb.b = action->sub.fade.end_value.b;
                    fade_complete = true;
                } else {
                    state->current_rgb.b += action->sub.fade.step;
                }
            } else if (action->sub.fade.start_value.b > action->sub.fade.end_value.b) {
                if (state->current_rgb.b <= action->sub.fade.step ||
                    state->current_rgb.b - action->sub.fade.step <= action->sub.fade.end_value.b) {
                    state->current_rgb.b = action->sub.fade.end_value.b;
                    fade_complete = true;
                } else {
                    state->current_rgb.b -= action->sub.fade.step;
                }
            }

            led_scene_output(&state->current_rgb);
            state->action_start_ms = led_scene_now_ms();

            if (fade_complete) {
                action_complete = true;
            }
        }
    }

    if (action_complete) {
        led_scene_advance_action();
    }
}

void led_scene_init(void)
{
    if (self.initialized) {
        return;
    }

    memset(&self, 0, sizeof(led_scene_self_t));
    self.active_scene = LED_SCENE_ID_MAX_NUM;

    (void)bsp_gpio_port_enable(LED_GPIO_PORT_MASK);
    bsp_gpio_commit_locked_pins(GPIO_RGB_LED_PORT, GPIO_RGB_LED_PIN);
    GPIOPinTypeGPIOOutput(GPIO_RGB_LED_PORT, GPIO_RGB_LED_PIN);
    GPIOPadConfigSet(GPIO_RGB_LED_PORT, GPIO_RGB_LED_PIN,
                     GPIO_STRENGTH_8MA, GPIO_PIN_TYPE_STD);
    *s_ws2812_data = 0U;

    if (ws2812b_init_user_buf(&s_ws2812,
                              LED_SCENE_PIXEL_COUNT,
                              s_ws2812_pixels,
                              sizeof(s_ws2812_pixels),
                              ws2812_pc3_transmit,
                              NULL) != WS2812B_OK) {
        LOG_WARN("led_scene: ws2812 init failed");
        return;
    }

    s_onoff_timer = xTimerCreate("led_onoff",
                                 pdMS_TO_TICKS(1000U),
                                 pdFALSE,
                                 NULL,
                                 led_scene_onoff_timer_cb);
    if (s_onoff_timer == NULL) {
        LOG_WARN("led_scene: timer create failed");
    }

    self.initialized = true;
    LOG_INFO("led_scene: ws2812 ready RGB_LED");
}

void led_scene_led_direct_set(led_scene_led_e led, bool on)
{
    led_rgb_value_t rgb = {0U, 0U, 0U};

    if (!self.initialized) {
        return;
    }

    if (led != LED_SCENE_LED_0) {
        return;
    }

    if (on) {
        rgb.r = LED_SCENE_RAINBOW_BRIGHT;
    }

    led_scene_output(&rgb);
}

void led_scene_rgb_set(uint8_t r, uint8_t g, uint8_t b)
{
    led_rgb_value_t rgb;

    if (!self.initialized) {
        return;
    }

    rgb.r = r;
    rgb.g = g;
    rgb.b = b;
    led_scene_output(&rgb);
}

void led_scene_run(led_scene_id_e id)
{
    if (id >= LED_SCENE_ID_MAX_NUM) {
        return;
    }

    if (!self.initialized) {
        return;
    }

    led_scene_state_t *state = &self.states[id];
    const led_scene_t *scene = scene_table[id].scene;

    if (scene == NULL) {
        return;
    }

    if (state->running) {
        return;
    }

    state->running = true;
    led_scene_reset_active_state(state, scene);

    led_scene_id_e new_scene = led_scene_find_highest_priority();
    if (new_scene != self.active_scene) {
        led_scene_timer_stop();
        if (self.active_scene < LED_SCENE_ID_MAX_NUM) {
            self.states[self.active_scene].running = false;
        }
        self.active_scene = new_scene;
    }

    if (self.active_scene < LED_SCENE_ID_MAX_NUM) {
        led_scene_reset_active_state(&self.states[self.active_scene],
                                     scene_table[self.active_scene].scene);
        led_scene_begin_current_action();
    }
}

bool led_scene_is_active(void)
{
    if (!self.initialized) {
        return false;
    }

    return led_scene_any_running();
}

void led_scene_cancel(led_scene_id_e id)
{
    if (id >= LED_SCENE_ID_MAX_NUM) {
        return;
    }

    if (!self.initialized) {
        return;
    }

    led_scene_state_t *state = &self.states[id];
    state->running = false;
    led_scene_timer_stop();

    if (self.active_scene == id) {
        led_scene_output(NULL);
        led_scene_id_e new_scene = led_scene_find_highest_priority();
        if (new_scene < LED_SCENE_ID_MAX_NUM) {
            self.active_scene = new_scene;
            led_scene_reset_active_state(&self.states[self.active_scene],
                                         scene_table[self.active_scene].scene);
            led_scene_begin_current_action();
        } else {
            self.active_scene = LED_SCENE_ID_MAX_NUM;
        }
    }
}
