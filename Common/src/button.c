#include "button.h"

#include "flexible_button.h"
#include "log.h"

#include "inc/hw_memmap.h"
#include "driverlib/gpio.h"

#include <string.h>

#define BTN_NUM 3

#define BTN_UP_PORT  GPIO_PORTB_BASE
#define BTN_UP_PIN   GPIO_PIN_4
#define BTN_OK_PORT  GPIO_PORTB_BASE
#define BTN_OK_PIN   GPIO_PIN_5
#define BTN_DN_PORT  GPIO_PORTC_BASE
#define BTN_DN_PIN   GPIO_PIN_5

typedef struct {
    btn_id_e id;
    const char *name;
    uint32_t port;
    uint8_t pin;
    uint8_t active_level;
    uint16_t permission;
    flex_button_t flex;
} button_list_t;

typedef struct {
    uint8_t num;
    button_list_t *list;
    btn_notify_t notify;
} button_item_t;

static button_list_t s_button_list[BTN_NUM];
static button_item_t self = {0};

static btn_id_e last_button_id = BTN_ID_MAX_NUMBER;
static btn_event_e last_button_event = BTN_EVENT_NONE;

static uint8_t button_read_level(uint32_t port, uint8_t pin)
{
    return (GPIOPinRead(port, pin) != 0U) ? 1U : 0U;
}

static uint8_t button_flex_read(void *flex)
{
    flex_button_t *btn = (flex_button_t *)flex;
    button_list_t *list = (button_list_t *)btn->user_data;
    return button_read_level(list->port, list->pin);
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
    list->flex.pressed_logic_level = (list->active_level ? 1u : 0u);
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
    s_button_list[0].port = BTN_UP_PORT;
    s_button_list[0].pin = BTN_UP_PIN;
    s_button_list[0].active_level = 0;
    s_button_list[0].permission = (uint16_t)BTN_PERMISSION_UP;

    s_button_list[1].id = BTN_ID_OK;
    s_button_list[1].name = "OK";
    s_button_list[1].port = BTN_OK_PORT;
    s_button_list[1].pin = BTN_OK_PIN;
    s_button_list[1].active_level = 0;
    s_button_list[1].permission = (uint16_t)BTN_PERMISSION_CONFIRM;

    s_button_list[2].id = BTN_ID_DN;
    s_button_list[2].name = "DN";
    s_button_list[2].port = BTN_DN_PORT;
    s_button_list[2].pin = BTN_DN_PIN;
    s_button_list[2].active_level = 0;
    s_button_list[2].permission = (uint16_t)BTN_PERMISSION_DOWN;

    self.num = BTN_NUM;
    self.list = s_button_list;
}

void button_init(btn_notify_t notify)
{
    uint8_t i;

    (void)memset(&self, 0, sizeof(button_item_t));
    button_config();
    self.notify = notify;

    for (i = 0U; i < BTN_NUM; i++) {
        button_flex_init(&s_button_list[i]);
    }
}

void button_schedule(void)
{
    if (self.list == NULL || self.num == 0U) {
        return;
    }
    (void)flex_button_scan();
}

void button_deinit(void)
{
    self.list = NULL;
    self.num = 0;
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

    for (i = 0U; i < self.num; i++) {
        button_list_t *p = &self.list[i];
        if (p->id == id) {
            return button_read_level(p->port, p->pin);
        }
    }

    return 0xFFU;
}
