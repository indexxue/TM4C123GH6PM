#include "button.h"

#include "flexible_button.h"
#include "log.h"

#include "board.h"
#include "bsp_adc.h"

#include "inc/hw_memmap.h"

#include <string.h>

#define BTN_NUM 3

/* PE2 / ADC1 AIN1：10k 上拉至 3.3V，按键电阻 10k / 5.1k / 1k 至 GND */
#define BTN_ADC_RAW_RELEASE_MIN   2800U
#define BTN_ADC_RAW_THRESH_UP_OK  1650U
#define BTN_ADC_RAW_THRESH_OK_DN   900U
#define BTN_ADC_RAW_THRESH_DN_MIN  250U

static const bsp_adc_channel_t s_button_adc_channel = { 1U, 0U };
static const bsp_adc_config_t s_button_adc_cfg = {
    .base = ADC1_BASE,
    .sequence = 0U,
    .channels = &s_button_adc_channel,
    .channel_count = 1U,
};

typedef struct {
    btn_id_e id;
    const char *name;
    uint16_t permission;
    flex_button_t flex;
} button_list_t;

typedef struct {
    uint8_t num;
    button_list_t *list;
    btn_notify_t notify;
    bool adc_ready;
} button_item_t;

static button_list_t s_button_list[BTN_NUM];
static button_item_t self = {0};

static btn_id_e last_button_id = BTN_ID_MAX_NUMBER;
static btn_event_e last_button_event = BTN_EVENT_NONE;
static btn_id_e s_adc_pressed_id = BTN_ID_MAX_NUMBER;
static uint32_t s_adc_last_raw = 0U;

static btn_id_e button_adc_decode(uint32_t raw)
{
    if (raw >= BTN_ADC_RAW_RELEASE_MIN) {
        return BTN_ID_MAX_NUMBER;
    }
    if (raw >= BTN_ADC_RAW_THRESH_UP_OK) {
        return BTN_ID_UP;
    }
    if (raw >= BTN_ADC_RAW_THRESH_OK_DN) {
        return BTN_ID_OK;
    }
    if (raw >= BTN_ADC_RAW_THRESH_DN_MIN) {
        return BTN_ID_DN;
    }
    return BTN_ID_MAX_NUMBER;
}

static void button_adc_sample(void)
{
    uint32_t raw;

    if (!self.adc_ready) {
        return;
    }

    if (!bsp_adc_sample_one(&s_button_adc_cfg, &raw)) {
        return;
    }

    s_adc_last_raw = raw;
    s_adc_pressed_id = button_adc_decode(raw);
}

static uint8_t button_flex_read(void *flex)
{
    flex_button_t *btn = (flex_button_t *)flex;
    button_list_t *list = (button_list_t *)btn->user_data;

    return (s_adc_pressed_id == list->id) ? 1U : 0U;
}

static void button_flex_event_callback(void *arg)
{
    flex_button_t *flex = (flex_button_t *)arg;
    button_list_t *list = (button_list_t *)flex->user_data;
    flex_button_event_t fevt = flex_button_event_read(flex);
    btn_event_e bevt = BTN_EVENT_NONE;

    switch (fevt) {
        case FLEX_BTN_PRESS_DOWN:
            bevt = BTN_EVENT_PRESS_DOWN;
            break;
        case FLEX_BTN_PRESS_CLICK:
            bevt = BTN_EVENT_SINGLE_CLICK;
            break;
        case FLEX_BTN_PRESS_DOUBLE_CLICK:
            bevt = BTN_EVENT_DOUBLE_CLICK;
            break;
        case FLEX_BTN_PRESS_REPEAT_CLICK:
            bevt = BTN_EVENT_REPEAT_CLICK;
            break;
        case FLEX_BTN_PRESS_LONG_START:
            bevt = BTN_EVENT_LONG_PRESS;
            break;
        case FLEX_BTN_PRESS_LONG_HOLD:
            bevt = BTN_EVENT_LONG_HOLD;
            break;
        case FLEX_BTN_PRESS_LONG_HOLD_UP:
            bevt = BTN_EVENT_LONG_HOLD_UP;
            break;
        default:
            bevt = BTN_EVENT_NONE;
            break;
    }

    if (bevt != BTN_EVENT_NONE) {
        last_button_id = list->id;
        last_button_event = bevt;
        if (self.notify != NULL) {
            self.notify(list->id, list->name, (btn_permission_e)list->permission, bevt);
        }
    }
}

static void button_flex_init(button_list_t *list)
{
    (void)memset(&list->flex, 0, sizeof(flex_button_t));
    list->flex.usr_button_read = button_flex_read;
    list->flex.cb = button_flex_event_callback;
    list->flex.pressed_logic_level = 1U;
    list->flex.debounce_tick = FLEX_MS_TO_SCAN_CNT(80);
    list->flex.max_multiple_clicks_interval = FLEX_MS_TO_SCAN_CNT(600);
    list->flex.short_press_start_tick = FLEX_MS_TO_SCAN_CNT(2000);
    list->flex.long_press_start_tick = FLEX_MS_TO_SCAN_CNT(10000);
    list->flex.long_hold_start_tick = FLEX_MS_TO_SCAN_CNT(11000);
    list->flex.user_data = list;
    (void)flex_button_register(&list->flex);
}

static void button_config(void)
{
    s_button_list[0].id = BTN_ID_UP;
    s_button_list[0].name = "UP";
    s_button_list[0].permission = (uint16_t)BTN_PERMISSION_UP;

    s_button_list[1].id = BTN_ID_OK;
    s_button_list[1].name = "OK";
    s_button_list[1].permission =
        (uint16_t)(BTN_PERMISSION_CONFIRM | BTN_PERMISSION_FTM);

    s_button_list[2].id = BTN_ID_DN;
    s_button_list[2].name = "DN";
    s_button_list[2].permission = (uint16_t)BTN_PERMISSION_DOWN;

    self.num = BTN_NUM;
    self.list = s_button_list;
}

void button_init(btn_notify_t notify)
{
    uint8_t i;
    uint32_t raw;

    (void)memset(&self, 0, sizeof(button_item_t));
    button_config();
    self.notify = notify;

    if (!bsp_adc_init(&s_button_adc_cfg)) {
        LOG_ERROR("button: ADC1 AIN1 init failed (PE2)");
        return;
    }

    self.adc_ready = true;
    if (bsp_adc_sample_one(&s_button_adc_cfg, &raw)) {
        s_adc_last_raw = raw;
        s_adc_pressed_id = button_adc_decode(raw);
    } else {
        LOG_WARN("button: ADC1 AIN1 @ PE2 init ok, first sample failed");
    }

    for (i = 0U; i < BTN_NUM; i++) {
        button_flex_init(&s_button_list[i]);
    }
}

void button_schedule(void)
{
    if (self.list == NULL || self.num == 0U) {
        return;
    }
    button_adc_sample();
    (void)flex_button_scan();
}

void button_deinit(void)
{
    self.list = NULL;
    self.num = 0;
    self.adc_ready = false;
    (void)memset(&self, 0, sizeof(button_item_t));
}

void button_last_event_get(btn_id_e *id, btn_event_e *event)
{
    if (id != NULL) {
        *id = last_button_id;
    }
    if (event != NULL) {
        *event = last_button_event;
    }
}

void button_last_event_clear(void)
{
    last_button_id = BTN_ID_MAX_NUMBER;
    last_button_event = BTN_EVENT_NONE;
}

const char *button_id_to_str(btn_id_e id)
{
    switch (id) {
        case BTN_ID_UP:
            return "UP";
        case BTN_ID_OK:
            return "OK";
        case BTN_ID_DN:
            return "DN";
        default:
            return "UNKNOWN";
    }
}

const char *button_event_to_str(btn_event_e event)
{
    switch (event) {
        case BTN_EVENT_NONE:
            return "NONE";
        case BTN_EVENT_PRESS_DOWN:
            return "PRESS_DOWN";
        case BTN_EVENT_PRESS_UP:
            return "PRESS_UP";
        case BTN_EVENT_SINGLE_CLICK:
            return "SINGLE_CLICK";
        case BTN_EVENT_DOUBLE_CLICK:
            return "DOUBLE_CLICK";
        case BTN_EVENT_REPEAT_CLICK:
            return "REPEAT_CLICK";
        case BTN_EVENT_LONG_PRESS:
            return "LONG_PRESS";
        case BTN_EVENT_LONG_HOLD:
            return "LONG_HOLD";
        case BTN_EVENT_LONG_HOLD_UP:
            return "LONG_HOLD_UP";
        default:
            return "UNKNOWN";
    }
}

void button_log_notify(btn_id_e id, const char *name, btn_permission_e permission, btn_event_e event)
{
    (void)permission;

    if (event == BTN_EVENT_SINGLE_CLICK || event == BTN_EVENT_DOUBLE_CLICK || event == BTN_EVENT_REPEAT_CLICK ||
        event == BTN_EVENT_LONG_PRESS || event == BTN_EVENT_LONG_HOLD || event == BTN_EVENT_LONG_HOLD_UP) {
        LOG_INFO("button id=%d(%s) name=%s event=%s", (int)id, button_id_to_str(id),
                 (name != NULL) ? name : "NULL", button_event_to_str(event));
    }
}

uint8_t button_get_level(btn_id_e id)
{
    uint8_t i;

    if (id >= BTN_ID_MAX_NUMBER || self.list == NULL) {
        return 0xFFU;
    }

    button_adc_sample();
    for (i = 0U; i < self.num; i++) {
        button_list_t *p = &self.list[i];
        if (p->id == id) {
            return (s_adc_pressed_id == p->id) ? 1U : 0U;
        }
    }

    return 0xFFU;
}

bool button_adc_raw_get(uint32_t *raw)
{
    if ((raw == NULL) || !self.adc_ready) {
        return false;
    }

    if (!bsp_adc_sample_one(&s_button_adc_cfg, raw)) {
        return false;
    }

    s_adc_last_raw = *raw;
    s_adc_pressed_id = button_adc_decode(*raw);
    return true;
}

btn_id_e button_adc_pressed_id(void)
{
    return s_adc_pressed_id;
}
