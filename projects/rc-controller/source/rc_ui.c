/**
 * @file    rc_ui.c
 * @brief   HOME 双十字 / 菜单导航 / 双杆校准向导
 */

#include "rc_ui.h"

#include "joy_cal.h"
#include "joystick.h"
#include "lcd_panel.h"

#include "battery.h"
#include "log.h"
#include "menu.h"
#include "proto_client.h"

#include <stdio.h>
#include <string.h>

#define RC_UI_MENU_HOLD_MS       1500U
#define RC_UI_BAT_POLL_MS        500U
#define RC_UI_NAV_THRESH         350
#define RC_UI_NAV_REARM          150
#define RC_UI_VISIBLE_ROWS       4U
/** 校准：相对回中点，每侧至少走这么多 ADC 才算推到位 */
#define RC_UI_CAL_SIDE_MIN       600U

typedef enum {
    CAL_STEP_CENTER = 0,
    CAL_STEP_EXTREMES,
    CAL_STEP_CONFIRM,
} cal_step_t;

static rc_ui_mode_t s_mode = RC_UI_MODE_HOME;
static menu_engine_t s_menu;
static uint16_t s_js2_hold_ms;
static uint16_t s_bat_poll_ms;
static uint32_t s_last_bat_mv;
static int16_t s_throttle;
static int16_t s_steer;
static bool_t s_nav_armed = TRUE;
static bool_t s_js1_btn_prev;
static bool_t s_js2_btn_prev;

static cal_step_t s_cal_step;
static bool_t s_cal_frame_dirty = TRUE;
static joy_cal_t s_cal_draft;
static uint16_t s_cal_min[JOY_CAL_AXIS_COUNT];
static uint16_t s_cal_max[JOY_CAL_AXIS_COUNT];
static bool_t s_menu_dirty = TRUE;

/* ---- menu callbacks ---- */

static void rc_ui_enter_home(void);
static void rc_ui_render_menu(void);
static void rc_ui_on_root_back(void *app_ctx, menu_engine_t *eng, const menu_page_t *page);

static void action_start_cal(void *app_ctx, menu_engine_t *eng, const menu_item_t *item);
static void action_restore_cal(void *app_ctx, menu_engine_t *eng, const menu_item_t *item);
static void action_reset_confirm(void *app_ctx, menu_engine_t *eng, const menu_item_t *item);

static int32_t param_deadband_get(void *app_ctx, const menu_item_t *item);
static void param_deadband_set(void *app_ctx, const menu_item_t *item, int32_t value);
static int32_t param_deadband_step(void *app_ctx, const menu_item_t *item, int32_t cur, int dir);

static int32_t param_invert_get(void *app_ctx, const menu_item_t *item);
static void param_invert_set(void *app_ctx, const menu_item_t *item, int32_t value);
static int32_t param_invert_step(void *app_ctx, const menu_item_t *item, int32_t cur, int dir);
static const char *param_invert_fmt(void *app_ctx, const menu_item_t *item, int32_t value,
                                    char *buf, size_t buflen);

static const menu_param_vtbl_t s_deadband_vtbl = {
    .get = param_deadband_get,
    .set = param_deadband_set,
    .step = param_deadband_step,
    .format = NULL,
};

static const menu_param_vtbl_t s_invert_vtbl = {
    .get = param_invert_get,
    .set = param_invert_set,
    .step = param_invert_step,
    .format = param_invert_fmt,
};

static const menu_item_t s_invert_items[] = {
    { .label = "J1X invert", .type = MENU_ITEM_PARAM, .param = &s_invert_vtbl,
      .user_ctx = (void *)(uintptr_t)JOY_CAL_AXIS_J1X },
    { .label = "J1Y invert", .type = MENU_ITEM_PARAM, .param = &s_invert_vtbl,
      .user_ctx = (void *)(uintptr_t)JOY_CAL_AXIS_J1Y },
    { .label = "J2X invert", .type = MENU_ITEM_PARAM, .param = &s_invert_vtbl,
      .user_ctx = (void *)(uintptr_t)JOY_CAL_AXIS_J2X },
    { .label = "J2Y invert", .type = MENU_ITEM_PARAM, .param = &s_invert_vtbl,
      .user_ctx = (void *)(uintptr_t)JOY_CAL_AXIS_J2Y },
};

static const menu_page_t s_invert_page = {
    .title = "Axis Invert",
    .items = s_invert_items,
    .count = (uint16_t)(sizeof(s_invert_items) / sizeof(s_invert_items[0])),
    .foot_hint = "JS1 adj  JS2 back",
};

static const menu_item_t s_reset_items[] = {
    { .label = "Cancel", .type = MENU_ITEM_ACTION, .on_select = action_restore_cal },
    { .label = "YES reset", .type = MENU_ITEM_ACTION, .flags = MENU_ITEM_F_DANGER,
      .on_select = action_reset_confirm },
};

static const menu_page_t s_reset_page = {
    .title = "Reset Cal?",
    .items = s_reset_items,
    .count = (uint16_t)(sizeof(s_reset_items) / sizeof(s_reset_items[0])),
    .foot_hint = "confirm danger",
};

static char s_monitor_line[4][28];

static const char *monitor_aux(void *app_ctx, const menu_item_t *item, char *buf, size_t buflen)
{
    uintptr_t idx = (uintptr_t)item->user_ctx;
    (void)app_ctx;
    (void)buf;
    (void)buflen;
    if (idx < 4U) {
        return s_monitor_line[idx];
    }
    return "";
}

static const menu_item_t s_monitor_items[] = {
    { .label = "J1X", .type = MENU_ITEM_LABEL, .aux = monitor_aux,
      .user_ctx = (void *)(uintptr_t)0 },
    { .label = "J1Y", .type = MENU_ITEM_LABEL, .aux = monitor_aux,
      .user_ctx = (void *)(uintptr_t)1 },
    { .label = "J2X", .type = MENU_ITEM_LABEL, .aux = monitor_aux,
      .user_ctx = (void *)(uintptr_t)2 },
    { .label = "J2Y", .type = MENU_ITEM_LABEL, .aux = monitor_aux,
      .user_ctx = (void *)(uintptr_t)3 },
};

static const menu_page_t s_monitor_page = {
    .title = "Monitor",
    .items = s_monitor_items,
    .count = (uint16_t)(sizeof(s_monitor_items) / sizeof(s_monitor_items[0])),
    .foot_hint = "JS2 back",
};

static const menu_item_t s_about_items[] = {
    { .label = "RC Controller", .type = MENU_ITEM_LABEL },
    { .label = "joy cal + menu", .type = MENU_ITEM_LABEL },
};

static const menu_page_t s_about_page = {
    .title = "About",
    .items = s_about_items,
    .count = (uint16_t)(sizeof(s_about_items) / sizeof(s_about_items[0])),
    .foot_hint = "JS2 back",
};

static const menu_item_t s_root_items[] = {
    { .label = "Calibrate...", .type = MENU_ITEM_ACTION, .on_select = action_start_cal },
    { .label = "Monitor", .type = MENU_ITEM_SUBMENU, .submenu = &s_monitor_page },
    { .label = "Deadband", .type = MENU_ITEM_PARAM, .param = &s_deadband_vtbl,
      .flags = MENU_ITEM_F_ENTER_NEXT },
    { .label = "Axis Invert", .type = MENU_ITEM_SUBMENU, .submenu = &s_invert_page },
    { .label = "Reset Cal", .type = MENU_ITEM_SUBMENU, .submenu = &s_reset_page,
      .flags = MENU_ITEM_F_DANGER },
    { .label = "About", .type = MENU_ITEM_SUBMENU, .submenu = &s_about_page },
};

static const menu_page_t s_root_page = {
    .title = "Settings",
    .items = s_root_items,
    .count = (uint16_t)(sizeof(s_root_items) / sizeof(s_root_items[0])),
    .foot_hint = "JS1 nav  JS2 back",
    .on_root_back = rc_ui_on_root_back,
};

static void rc_ui_enter_home(void)
{
    s_mode = RC_UI_MODE_HOME;
    s_js2_hold_ms = 0U;
    s_bat_poll_ms = RC_UI_BAT_POLL_MS;
    lcd_panel_show_home();
    LOG_INFO("rc_ui: HOME");
}

static void rc_ui_on_root_back(void *app_ctx, menu_engine_t *eng, const menu_page_t *page)
{
    (void)app_ctx;
    (void)eng;
    (void)page;
    rc_ui_enter_home();
}

static void rc_ui_enter_menu(void)
{
    menu_engine_opts_t opts;

    s_mode = RC_UI_MODE_MENU;
    s_js2_hold_ms = 0U;
    s_nav_armed = TRUE;
    (void)memset(&opts, 0, sizeof(opts));
    opts.wrap_around = 1U;
    opts.param_on_vertical = 0U;
    menu_engine_reset(&s_menu);
    menu_engine_configure(&s_menu, &opts);
    s_menu_dirty = TRUE;
    LOG_INFO("rc_ui: MENU");
}

static void action_start_cal(void *app_ctx, menu_engine_t *eng, const menu_item_t *item)
{
    size_t i;
    (void)app_ctx;
    (void)eng;
    (void)item;

    joy_cal_get_default(&s_cal_draft);
    for (i = 0U; i < JOY_CAL_AXIS_COUNT; i++) {
        s_cal_min[i] = 4095U;
        s_cal_max[i] = 0U;
    }
    s_cal_step = CAL_STEP_CENTER;
    s_cal_frame_dirty = TRUE;
    s_mode = RC_UI_MODE_CAL;
    LOG_INFO("rc_ui: CAL center");
}

static void action_restore_cal(void *app_ctx, menu_engine_t *eng, const menu_item_t *item)
{
    (void)app_ctx;
    (void)item;
    (void)menu_nav_back(eng);
    s_menu_dirty = TRUE;
}

static void action_reset_confirm(void *app_ctx, menu_engine_t *eng, const menu_item_t *item)
{
    (void)app_ctx;
    (void)item;
    if (joy_cal_restore_default() == STATUS_OK) {
        LOG_INFO("rc_ui: cal restored default");
    }
    menu_engine_reset(eng);
    s_menu_dirty = TRUE;
}

static int32_t param_deadband_get(void *app_ctx, const menu_item_t *item)
{
    (void)app_ctx;
    (void)item;
    return (int32_t)joy_cal_get()->deadband;
}

static void param_deadband_set(void *app_ctx, const menu_item_t *item, int32_t value)
{
    (void)app_ctx;
    (void)item;
    if (value < 0) {
        value = 0;
    }
    if (value > 400) {
        value = 400;
    }
    (void)joy_cal_set_deadband((uint16_t)value);
}

static int32_t param_deadband_step(void *app_ctx, const menu_item_t *item, int32_t cur, int dir)
{
    int32_t next = cur + (dir * 10);
    (void)app_ctx;
    (void)item;
    if (next < 0) {
        next = 0;
    }
    if (next > 400) {
        next = 400;
    }
    return next;
}

static int32_t param_invert_get(void *app_ctx, const menu_item_t *item)
{
    joy_cal_axis_id_t axis = (joy_cal_axis_id_t)(uintptr_t)item->user_ctx;
    (void)app_ctx;
    return joy_cal_axis_inverted(axis) ? 1 : 0;
}

static void param_invert_set(void *app_ctx, const menu_item_t *item, int32_t value)
{
    joy_cal_axis_id_t axis = (joy_cal_axis_id_t)(uintptr_t)item->user_ctx;
    bool_t want = (value != 0) ? TRUE : FALSE;
    (void)app_ctx;
    if (joy_cal_axis_inverted(axis) != want) {
        (void)joy_cal_toggle_invert(axis);
    }
}

static int32_t param_invert_step(void *app_ctx, const menu_item_t *item, int32_t cur, int dir)
{
    (void)app_ctx;
    (void)item;
    (void)dir;
    return (cur != 0) ? 0 : 1;
}

static const char *param_invert_fmt(void *app_ctx, const menu_item_t *item, int32_t value,
                                    char *buf, size_t buflen)
{
    (void)app_ctx;
    (void)item;
    if ((buf == NULL) || (buflen == 0U)) {
        return value ? "ON" : "OFF";
    }
    (void)snprintf(buf, buflen, "%s", value ? "ON" : "OFF");
    return buf;
}

static bool_t rc_ui_update_monitor_cache(const board_joystick_state_t js[BOARD_JOYSTICK_COUNT])
{
    char next[4][28];
    bool_t changed = FALSE;
    uint8_t i;

    (void)snprintf(next[0], sizeof(next[0]), "%u/%d",
                   (unsigned)js[0].raw.x_raw, (int)js[0].mapped.x_cmd);
    (void)snprintf(next[1], sizeof(next[1]), "%u/%d",
                   (unsigned)js[0].raw.y_raw, (int)js[0].mapped.y_cmd);
    (void)snprintf(next[2], sizeof(next[2]), "%u/%d",
                   (unsigned)js[1].raw.x_raw, (int)js[1].mapped.x_cmd);
    (void)snprintf(next[3], sizeof(next[3]), "%u/%d",
                   (unsigned)js[1].raw.y_raw, (int)js[1].mapped.y_cmd);

    for (i = 0U; i < 4U; i++) {
        if (strcmp(next[i], s_monitor_line[i]) != 0) {
            changed = TRUE;
            (void)strncpy(s_monitor_line[i], next[i], sizeof(s_monitor_line[i]) - 1U);
            s_monitor_line[i][sizeof(s_monitor_line[i]) - 1U] = '\0';
        }
    }
    return changed;
}

static void rc_ui_render_menu(void)
{
    const menu_page_t *page = menu_current_page(&s_menu);
    const char *lines[RC_UI_VISIBLE_ROWS];
    char auxbuf[4][24];
    char rowbuf[4][36];
    uint16_t focus_pos = 0U;
    uint16_t first = 0U;
    uint16_t vis_count;
    uint8_t i;
    uint8_t cursor_vis = 0U;

    if (page == NULL) {
        return;
    }

    (void)menu_page_focus_cursor_pos(page, menu_current_index(&s_menu), &focus_pos);
    (void)menu_page_viewport_first_focus(page, menu_current_index(&s_menu), RC_UI_VISIBLE_ROWS,
                                         &first);
    vis_count = menu_page_focus_item_count(page);

    for (i = 0U; i < RC_UI_VISIBLE_ROWS; i++) {
        uint16_t focus_slot = (uint16_t)(first + i);
        uint16_t abs_idx = 0U;
        const menu_item_t *it;
        const char *aux;

        lines[i] = "";
        if (focus_slot >= vis_count) {
            continue;
        }
        if (!menu_page_focus_item_at(page, focus_slot, &abs_idx)) {
            continue;
        }
        it = &page->items[abs_idx];
        aux = menu_item_aux_text(NULL, it, auxbuf[i], sizeof(auxbuf[i]));
        if ((aux != NULL) && (aux[0] != '\0')) {
            (void)snprintf(rowbuf[i], sizeof(rowbuf[i]), "%s %s", it->label, aux);
        } else if (it->type == MENU_ITEM_PARAM && it->param != NULL && it->param->get != NULL) {
            int32_t v = it->param->get(NULL, it);
            char fbuf[16];
            const char *ft = NULL;
            if (it->param->format != NULL) {
                ft = it->param->format(NULL, it, v, fbuf, sizeof(fbuf));
            }
            if (ft != NULL) {
                (void)snprintf(rowbuf[i], sizeof(rowbuf[i]), "%s %s", it->label, ft);
            } else {
                (void)snprintf(rowbuf[i], sizeof(rowbuf[i]), "%s %ld", it->label, (long)v);
            }
        } else {
            (void)snprintf(rowbuf[i], sizeof(rowbuf[i]), "%s", it->label);
        }
        lines[i] = rowbuf[i];
        if (focus_slot == focus_pos) {
            cursor_vis = i;
        }
    }

    lcd_panel_show_menu(page->title, lines, RC_UI_VISIBLE_ROWS, cursor_vis, page->foot_hint);
    s_menu_dirty = FALSE;
}

static void rc_ui_menu_nav_from_stick(const board_joystick_state_t *js1)
{
    menu_result_t r = MENU_RESULT_NONE;

    if (js1 == NULL) {
        return;
    }

    if (!s_nav_armed) {
        if ((js1->mapped.x_cmd > -RC_UI_NAV_REARM) && (js1->mapped.x_cmd < RC_UI_NAV_REARM) &&
            (js1->mapped.y_cmd > -RC_UI_NAV_REARM) && (js1->mapped.y_cmd < RC_UI_NAV_REARM)) {
            s_nav_armed = TRUE;
        }
        return;
    }

    if (js1->mapped.y_cmd >= RC_UI_NAV_THRESH) {
        r = menu_dispatch(&s_menu, MENU_EVT_UP);
        s_nav_armed = FALSE;
    } else if (js1->mapped.y_cmd <= -RC_UI_NAV_THRESH) {
        r = menu_dispatch(&s_menu, MENU_EVT_DOWN);
        s_nav_armed = FALSE;
    } else if (js1->mapped.x_cmd >= RC_UI_NAV_THRESH) {
        r = menu_dispatch(&s_menu, MENU_EVT_RIGHT);
        s_nav_armed = FALSE;
    } else if (js1->mapped.x_cmd <= -RC_UI_NAV_THRESH) {
        r = menu_dispatch(&s_menu, MENU_EVT_LEFT);
        s_nav_armed = FALSE;
    }

    if ((r != MENU_RESULT_NONE) && (r != MENU_RESULT_IGNORED)) {
        if (r == MENU_RESULT_EXIT) {
            return;
        }
        s_menu_dirty = TRUE;
    }
}

/** 相对回中：两侧行程均 ≥ RC_UI_CAL_SIDE_MIN 才算该轴合格 */
static bool_t rc_ui_cal_axis_span_ok(uint16_t center, uint16_t lo, uint16_t hi)
{
    if (lo > center) {
        return FALSE;
    }
    if (hi < center) {
        return FALSE;
    }
    if ((uint16_t)(center - lo) < RC_UI_CAL_SIDE_MIN) {
        return FALSE;
    }
    if ((uint16_t)(hi - center) < RC_UI_CAL_SIDE_MIN) {
        return FALSE;
    }
    return TRUE;
}

static void rc_ui_cal_tick(const board_joystick_state_t js[BOARD_JOYSTICK_COUNT],
                           bool_t js1_edge, bool_t js2_edge)
{
    const char *hint;
    char status[36];
    uint16_t raw[JOY_CAL_AXIS_COUNT];
    size_t i;
    uint8_t ready_n;
    bool_t all_ready;

    raw[0] = js[0].raw.x_raw;
    raw[1] = js[0].raw.y_raw;
    raw[2] = js[1].raw.x_raw;
    raw[3] = js[1].raw.y_raw;

    if (s_cal_step == CAL_STEP_EXTREMES) {
        for (i = 0U; i < JOY_CAL_AXIS_COUNT; i++) {
            if (raw[i] < s_cal_min[i]) {
                s_cal_min[i] = raw[i];
            }
            if (raw[i] > s_cal_max[i]) {
                s_cal_max[i] = raw[i];
            }
        }
    }

    ready_n = 0U;
    for (i = 0U; i < JOY_CAL_AXIS_COUNT; i++) {
        if (rc_ui_cal_axis_span_ok(s_cal_draft.axis[i].center, s_cal_min[i], s_cal_max[i])) {
            ready_n++;
        }
    }
    all_ready = (ready_n >= JOY_CAL_AXIS_COUNT) ? TRUE : FALSE;

    if (js2_edge) {
        s_mode = RC_UI_MODE_MENU;
        s_menu_dirty = TRUE;
        LOG_INFO("rc_ui: CAL cancel");
        return;
    }

    if (js1_edge) {
        if (s_cal_step == CAL_STEP_CENTER) {
            for (i = 0U; i < JOY_CAL_AXIS_COUNT; i++) {
                s_cal_draft.axis[i].center = raw[i];
                s_cal_min[i] = raw[i];
                s_cal_max[i] = raw[i];
            }
            s_cal_step = CAL_STEP_EXTREMES;
            s_cal_frame_dirty = TRUE;
            LOG_INFO("rc_ui: CAL extremes");
        } else if (s_cal_step == CAL_STEP_EXTREMES) {
            if (!all_ready) {
                LOG_WARN("rc_ui: CAL extremes incomplete %u/%u",
                         (unsigned)ready_n, (unsigned)JOY_CAL_AXIS_COUNT);
            } else {
                for (i = 0U; i < JOY_CAL_AXIS_COUNT; i++) {
                    s_cal_draft.axis[i].min = s_cal_min[i];
                    s_cal_draft.axis[i].max = s_cal_max[i];
                }
                s_cal_step = CAL_STEP_CONFIRM;
                s_cal_frame_dirty = TRUE;
                LOG_INFO("rc_ui: CAL confirm");
            }
        } else {
            /* 保存前再验一遍行程 */
            all_ready = TRUE;
            for (i = 0U; i < JOY_CAL_AXIS_COUNT; i++) {
                if (!rc_ui_cal_axis_span_ok(s_cal_draft.axis[i].center,
                                            s_cal_draft.axis[i].min,
                                            s_cal_draft.axis[i].max)) {
                    all_ready = FALSE;
                    break;
                }
            }
            if (!all_ready) {
                LOG_WARN("rc_ui: CAL save blocked (span)");
                s_cal_step = CAL_STEP_EXTREMES;
                s_cal_frame_dirty = TRUE;
            } else {
                s_cal_draft.deadband = joy_cal_get()->deadband;
                s_cal_draft.invert_mask = joy_cal_get()->invert_mask;
                if (joy_cal_save(&s_cal_draft) == STATUS_OK) {
                    LOG_INFO("rc_ui: CAL saved");
                } else {
                    LOG_WARN("rc_ui: CAL save fail");
                }
                s_mode = RC_UI_MODE_MENU;
                s_menu_dirty = TRUE;
                return;
            }
        }
    }

    /* 重新统计（步骤可能刚变） */
    ready_n = 0U;
    for (i = 0U; i < JOY_CAL_AXIS_COUNT; i++) {
        uint16_t lo = (s_cal_step == CAL_STEP_CONFIRM) ? s_cal_draft.axis[i].min : s_cal_min[i];
        uint16_t hi = (s_cal_step == CAL_STEP_CONFIRM) ? s_cal_draft.axis[i].max : s_cal_max[i];

        if (rc_ui_cal_axis_span_ok(s_cal_draft.axis[i].center, lo, hi)) {
            ready_n++;
        }
    }
    all_ready = (ready_n >= JOY_CAL_AXIS_COUNT) ? TRUE : FALSE;

    if (s_cal_step == CAL_STEP_CENTER) {
        hint = "Center both sticks";
        (void)snprintf(status, sizeof(status), "JS1=ok  JS2=cancel");
    } else if (s_cal_step == CAL_STEP_EXTREMES) {
        hint = "Push all extremes";
        if (all_ready) {
            (void)snprintf(status, sizeof(status), "ok 4/4  JS1=next");
        } else {
            (void)snprintf(status, sizeof(status), "reach %u/4  keep push",
                           (unsigned)ready_n);
        }
    } else {
        hint = "Save calibration?";
        (void)snprintf(status, sizeof(status), "JS1=save JS2=cancel");
    }

    if (s_cal_frame_dirty) {
        lcd_panel_show_cal_frame((s_cal_step == CAL_STEP_CENTER) ? "Cal 1/3" :
                                     (s_cal_step == CAL_STEP_EXTREMES) ? "Cal 2/3" : "Cal 3/3",
                                 hint);
        s_cal_frame_dirty = FALSE;
    }
    (void)lcd_panel_update_cal(js[0].mapped.x_cmd, js[0].mapped.y_cmd,
                              js[1].mapped.x_cmd, js[1].mapped.y_cmd,
                              status);
}

status_t rc_ui_init(void)
{
    menu_engine_init(&s_menu, &s_root_page, NULL);
    rc_ui_enter_home();
    return STATUS_OK;
}

void rc_ui_tick(uint32_t dt_ms)
{
    board_joystick_state_t js[BOARD_JOYSTICK_COUNT];
    bool_t js1_edge;
    bool_t js2_edge;
    bool_t js2_down;

    if (!board_joystick_sample(js)) {
        return;
    }

    s_throttle = js[BOARD_JOYSTICK_1].mapped.y_cmd;
    s_steer = js[BOARD_JOYSTICK_1].mapped.x_cmd;

    js2_down = js[BOARD_JOYSTICK_2].raw.btn_pressed;
    js1_edge = (js[BOARD_JOYSTICK_1].raw.btn_pressed && !s_js1_btn_prev) ? TRUE : FALSE;
    js2_edge = (js2_down && !s_js2_btn_prev) ? TRUE : FALSE;
    s_js1_btn_prev = js[BOARD_JOYSTICK_1].raw.btn_pressed;
    s_js2_btn_prev = js2_down;

    if (s_mode == RC_UI_MODE_HOME) {
        battery_voltage_t bat;

        if (js2_down) {
            s_js2_hold_ms = (uint16_t)(s_js2_hold_ms + dt_ms);
            if (s_js2_hold_ms >= RC_UI_MENU_HOLD_MS) {
                rc_ui_enter_menu();
                return;
            }
        } else {
            s_js2_hold_ms = 0U;
        }

        s_bat_poll_ms = (uint16_t)(s_bat_poll_ms + dt_ms);
        if (s_bat_poll_ms >= RC_UI_BAT_POLL_MS) {
            s_bat_poll_ms = 0U;
            if (battery_voltage_read_mv(&bat) > 0U) {
                s_last_bat_mv = bat.current_mv;
            }
        }

        (void)lcd_panel_update_home(js[0].mapped.x_cmd, js[0].mapped.y_cmd,
                                    js[1].mapped.x_cmd, js[1].mapped.y_cmd,
                                    js[0].mapped.btn_pressed, js[1].mapped.btn_pressed,
                                    s_last_bat_mv, proto_client_link_up());
        return;
    }

    if (s_mode == RC_UI_MODE_CAL) {
        rc_ui_cal_tick(js, js1_edge, js2_edge);
        return;
    }

    /* MENU */
    if (js1_edge) {
        menu_result_t r = menu_dispatch(&s_menu, MENU_EVT_ENTER);
        if (r == MENU_RESULT_EXIT) {
            return;
        }
        if (s_mode == RC_UI_MODE_CAL) {
            return;
        }
        s_menu_dirty = TRUE;
    }
    if (js2_edge) {
        menu_result_t r = menu_dispatch(&s_menu, MENU_EVT_BACK);
        if (r == MENU_RESULT_EXIT) {
            return;
        }
        s_menu_dirty = TRUE;
    }

    rc_ui_menu_nav_from_stick(&js[BOARD_JOYSTICK_1]);

    if (menu_current_page(&s_menu) == &s_monitor_page) {
        if (rc_ui_update_monitor_cache(js)) {
            s_menu_dirty = TRUE;
        }
    }
    if (s_menu_dirty) {
        rc_ui_render_menu();
    }
}

rc_ui_mode_t rc_ui_mode(void)
{
    return s_mode;
}

bool_t rc_ui_drive_muted(void)
{
    return (s_mode != RC_UI_MODE_HOME) ? TRUE : FALSE;
}

int16_t rc_ui_last_throttle(void)
{
    return s_throttle;
}

int16_t rc_ui_last_steer(void)
{
    return s_steer;
}
