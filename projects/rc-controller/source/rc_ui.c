/**
 * @file    rc_ui.c
 * @brief   HOME(IDLE) / DRIVE / 菜单导航 / 双杆校准向导
 */

#include "rc_ui.h"

#include "joy_cal.h"
#include "joystick.h"
#include "lcd_panel.h"
#include "rc_input.h"
#include "rc_link.h"
#include "rc_model.h"
#include "rc_sub.h"
#include "screen/screen_home.h"
#include "screen/screen_target.h"
#include "rc_mixer.h"

#include "log.h"
#include "menu.h"
#include "nvs.h"

#include <stdio.h>
#include <string.h>

#define RC_UI_TIP_MS             1500U
#define RC_UI_NAV_THRESH         350
#define RC_UI_NAV_REARM          150
#define RC_UI_VISIBLE_ROWS       4U
/** 校准：相对回中点，每侧至少走这么多 ADC 才算推到位 */
#define RC_UI_CAL_SIDE_MIN       600U
/** Monitor：抑制 ADC 噪声；仅超门限的行做局部重绘 */
#define RC_UI_MON_RAW_STEP       8U
#define RC_UI_MON_CMD_STEP       10

#define RC_UI_SUB_ITEM_BAT       0U
#define RC_UI_SUB_ITEM_US        1U
#define RC_UI_SUB_ITEM_RPM       2U
#define RC_UI_SUB_ITEM_SAVE      3U
#define RC_UI_SUB_ITEM_CANCEL    4U
#define RC_UI_SUB_ITEM_COUNT     5U

typedef enum {
    CAL_STEP_CENTER = 0,
    CAL_STEP_EXTREMES,
    CAL_STEP_CONFIRM,
} cal_step_t;

static rc_ui_mode_t s_mode = RC_UI_MODE_HOME;
static menu_engine_t s_menu;
static uint16_t s_tip_ms;
static const char *s_tip;
static int16_t s_throttle;
static int16_t s_steer;
static bool_t s_nav_armed = TRUE;
/** 上一拍链路状态：用于 DRIVE 内闪断边沿（停驶 / 恢复后重订） */
static bool_t s_link_up_prev;

static uint32_t s_sub_draft;
static uint8_t s_sub_cursor;
static bool_t s_sub_dirty = TRUE;

static cal_step_t s_cal_step;
static bool_t s_cal_frame_dirty = TRUE;
static joy_cal_t s_cal_draft;
static uint16_t s_cal_min[JOY_CAL_AXIS_COUNT];
static uint16_t s_cal_max[JOY_CAL_AXIS_COUNT];
static bool_t s_menu_dirty = TRUE;
static bool_t s_sub_return_drive = TRUE;

/* ---- menu callbacks ---- */

static void rc_ui_enter_home(void);
static void rc_ui_enter_drive(void);
static void rc_ui_disarm_to_home(void);
static void rc_ui_enter_subscribe(void);
static void rc_ui_leave_subscribe(bool_t save);
static void rc_ui_set_tip(const char *tip);
static void rc_ui_render_subscribe(void);
static void rc_ui_subscribe_nav(const board_joystick_state_t *js1);
static void rc_ui_render_menu(void);
static void rc_ui_on_root_back(void *app_ctx, menu_engine_t *eng, const menu_page_t *page);

static void action_start_cal(void *app_ctx, menu_engine_t *eng, const menu_item_t *item);
static void action_restore_cal(void *app_ctx, menu_engine_t *eng, const menu_item_t *item);
static void action_reset_confirm(void *app_ctx, menu_engine_t *eng, const menu_item_t *item);
static void action_switch_model(void *app_ctx, menu_engine_t *eng, const menu_item_t *item);
static void action_open_subscribe(void *app_ctx, menu_engine_t *eng, const menu_item_t *item);

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
static uint16_t s_mon_raw[4];
static int16_t s_mon_cmd[4];
static bool_t s_mon_cache_valid;

static const char *const s_mon_labels[4] = {"J1X", "J1Y", "J2X", "J2Y"};

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

static bool_t rc_ui_delta_u16(uint16_t a, uint16_t b, uint16_t thresh)
{
    uint16_t lo = (a < b) ? a : b;
    uint16_t hi = (a < b) ? b : a;
    return ((uint16_t)(hi - lo) >= thresh) ? TRUE : FALSE;
}

static bool_t rc_ui_delta_i16(int16_t a, int16_t b, int16_t thresh)
{
    int16_t d = (int16_t)(a - b);
    if (d < 0) {
        d = (int16_t)(-d);
    }
    return (d >= thresh) ? TRUE : FALSE;
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

static const char *about_fw_aux(void *app_ctx, const menu_item_t *item, char *buf, size_t buflen)
{
    const nvs_cfg_t *cfg = nvs_cfg_get();
    (void)app_ctx;
    (void)item;
    if ((buf == NULL) || (buflen == 0U)) {
        return (cfg->fw_version[0] != '\0') ? cfg->fw_version : "-";
    }
    (void)snprintf(buf, buflen, "%s",
                   (cfg->fw_version[0] != '\0') ? cfg->fw_version : "-");
    return buf;
}

static const char *about_sn_aux(void *app_ctx, const menu_item_t *item, char *buf, size_t buflen)
{
    const nvs_cfg_t *cfg = nvs_cfg_get();
    (void)app_ctx;
    (void)item;
    if ((buf == NULL) || (buflen == 0U)) {
        return (cfg->serial[0] != '\0') ? cfg->serial : "-";
    }
    (void)snprintf(buf, buflen, "%s",
                   (cfg->serial[0] != '\0') ? cfg->serial : "-");
    return buf;
}

static const menu_item_t s_about_items[] = {
    { .label = "RC Controller", .type = MENU_ITEM_LABEL },
    { .label = "fw", .type = MENU_ITEM_LABEL, .aux = about_fw_aux },
    { .label = "sn", .type = MENU_ITEM_LABEL, .aux = about_sn_aux },
};

static const menu_page_t s_about_page = {
    .title = "About",
    .items = s_about_items,
    .count = (uint16_t)(sizeof(s_about_items) / sizeof(s_about_items[0])),
    .foot_hint = "JS2 back",
};

static const char *model_menu_aux(void *app_ctx, const menu_item_t *item, char *buf, size_t buflen)
{
    const rc_model_t *m = rc_model_active();
    (void)app_ctx;
    (void)item;
    if (m == NULL) {
        return "-";
    }
    if ((buf == NULL) || (buflen == 0U)) {
        return m->name;
    }
    (void)snprintf(buf, buflen, "%s %u/%u", m->name,
                   (unsigned)(rc_model_active_index() + 1U), (unsigned)rc_model_count());
    return buf;
}

static const menu_item_t s_root_items[] = {
    { .label = "Switch Model", .type = MENU_ITEM_ACTION, .on_select = action_switch_model,
      .aux = model_menu_aux },
    { .label = "Subscribe...", .type = MENU_ITEM_ACTION, .on_select = action_open_subscribe },
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
    s_throttle = 0;
    s_steer = 0;
    s_tip = NULL;
    s_tip_ms = 0U;
    screen_target_close();
    screen_home_show();
    LOG_INFO("rc_ui: HOME");
}

static void rc_ui_enter_drive(void)
{
    s_mode = RC_UI_MODE_DRIVE;
    s_tip = NULL;
    s_tip_ms = 0U;
    s_throttle = 0;
    s_steer = 0;
    s_link_up_prev = rc_link_up();
    (void)rc_link_subscribe(rc_sub_get_mask());
    lcd_panel_show_drive();
    LOG_INFO("rc_ui: DRIVE");
}

static void rc_ui_disarm_to_home(void)
{
    s_throttle = 0;
    s_steer = 0;
    (void)rc_link_send_drive_stop();
    (void)rc_link_unsubscribe_optional();
    rc_link_telem_clear();
    s_mode = RC_UI_MODE_HOME;
    s_tip = NULL;
    s_tip_ms = 0U;
    screen_home_show();
    LOG_INFO("rc_ui: HOME (disarm, link kept)");
}

static void rc_ui_enter_subscribe(void)
{
    s_mode = RC_UI_MODE_SUBSCRIBE;
    s_nav_armed = TRUE;
    s_sub_draft = rc_sub_get_mask();
    s_sub_cursor = 0U;
    s_sub_dirty = TRUE;
    s_throttle = 0;
    s_steer = 0;
    s_link_up_prev = rc_link_up();
    (void)rc_link_send_drive_stop();
    LOG_INFO("rc_ui: SUBSCRIBE");
}

static void rc_ui_leave_subscribe(bool_t save)
{
    if (save != FALSE) {
        if (rc_sub_save_mask(s_sub_draft) == STATUS_OK) {
            (void)rc_link_subscribe(rc_sub_get_mask());
        }
    }
    if (s_sub_return_drive != FALSE) {
        s_mode = RC_UI_MODE_DRIVE;
        s_nav_armed = TRUE;
        s_link_up_prev = rc_link_up();
        lcd_panel_show_drive();
        LOG_INFO("rc_ui: DRIVE (from sub)");
    } else {
        s_mode = RC_UI_MODE_MENU;
        s_menu_dirty = TRUE;
        LOG_INFO("rc_ui: MENU (from sub)");
    }
}

static void rc_ui_set_tip(const char *tip)
{
    s_tip = tip;
    s_tip_ms = (tip != NULL) ? RC_UI_TIP_MS : 0U;
}

static void rc_ui_switch_model(bool_t in_drive)
{
    const rc_model_t *model;

    if (in_drive != FALSE) {
        s_throttle = 0;
        s_steer = 0;
        (void)rc_link_send_drive_stop();
    }

    if (rc_model_cycle_next() != STATUS_OK) {
        rc_ui_set_tip("MODEL?");
        return;
    }

    rc_mixer_on_model_changed();

    model = rc_model_active();
    if (model != NULL) {
        LOG_INFO("rc_ui: model -> %s", model->name);
    }

    /* 模型名已在顶栏显示；切换模型时不弹 tip */
    if (in_drive == FALSE) {
        s_tip = NULL;
        s_tip_ms = 0U;
    }

    if ((in_drive != FALSE) && (rc_link_up() != FALSE)) {
        (void)rc_link_subscribe(rc_sub_get_mask());
    }
}

static bool_t rc_ui_drive_sticks_released(const board_joystick_state_t *js)
{
    const rc_model_t *model = rc_model_active();

    if (model == NULL) {
        return FALSE;
    }
    if (model->input_src == RC_MODEL_INPUT_IMU_TILT) {
        if ((s_throttle > -80) && (s_throttle < 80) && (s_steer > -80) && (s_steer < 80)) {
            return TRUE;
        }
        return FALSE;
    }
    return rc_input_sticks_centered(js);
}

static bool_t rc_ui_tilt_arm_ready(void)
{
    if (rc_mixer_attitude_ready() == FALSE) {
        return FALSE;
    }
    return rc_mixer_zero_ready();
}

static bool_t rc_ui_tip_active(uint32_t dt_ms, const char **tip_out)
{
    const char *tip = NULL;

    if (s_tip_ms > 0U) {
        if (s_tip_ms > dt_ms) {
            s_tip_ms = (uint16_t)(s_tip_ms - dt_ms);
            tip = s_tip;
        } else {
            s_tip_ms = 0U;
            s_tip = NULL;
        }
    }
    if (tip_out != NULL) {
        *tip_out = tip;
    }
    return (tip != NULL) ? TRUE : FALSE;
}

static void rc_ui_paint_drive(void)
{
    proto_client_telem_t t;
    char bat[16];
    char us[16];
    char att[24];
    char spd[32];
    char enc[24];
    int32_t left_rpm;
    int32_t right_rpm;

    rc_link_telem_get(&t);

    if (t.bat_valid != FALSE) {
        (void)snprintf(bat, sizeof(bat), "%u%%", (unsigned)t.bat_pct);
    } else {
        (void)snprintf(bat, sizeof(bat), "null");
    }

    if (t.us_valid != FALSE) {
        (void)snprintf(us, sizeof(us), "%umm", (unsigned)t.us_mm);
    } else {
        (void)snprintf(us, sizeof(us), "null");
    }

    if (t.att_valid != FALSE) {
        (void)snprintf(att, sizeof(att), "%d/%d/%d", (int)t.roll, (int)t.pitch, (int)t.yaw);
    } else {
        (void)snprintf(att, sizeof(att), "null");
    }

    if (t.rpm_valid != FALSE) {
        left_rpm = (t.rpm[0] + t.rpm[2]) / 2;
        right_rpm = (t.rpm[1] + t.rpm[3]) / 2;
        (void)snprintf(spd, sizeof(spd), "L%ld R%ld", (long)left_rpm, (long)right_rpm);
    } else {
        (void)snprintf(spd, sizeof(spd), "null");
    }

    if (t.enc_valid != FALSE) {
        (void)snprintf(enc, sizeof(enc), "%lu",
                       (unsigned long)((t.enc[0] + t.enc[1] + t.enc[2] + t.enc[3]) / 4U));
    } else {
        (void)snprintf(enc, sizeof(enc), "null");
    }

    (void)lcd_panel_update_drive(rc_link_up(), bat, us, att, spd, enc, s_throttle,
                                 s_steer);
}

static void rc_ui_render_subscribe(void)
{
    char lines[RC_UI_SUB_ITEM_COUNT][28];
    const char *ptrs[RC_UI_SUB_ITEM_COUNT];
    uint8_t i;

    (void)snprintf(lines[RC_UI_SUB_ITEM_BAT], sizeof(lines[0]), "%c Battery",
                   ((s_sub_draft & PROTO_CLIENT_CH_BATTERY) != 0U) ? 'x' : ' ');
    (void)snprintf(lines[RC_UI_SUB_ITEM_US], sizeof(lines[0]), "%c Ultrasonic",
                   ((s_sub_draft & PROTO_CLIENT_CH_ULTRASONIC) != 0U) ? 'x' : ' ');
    (void)snprintf(lines[RC_UI_SUB_ITEM_RPM], sizeof(lines[0]), "%c Motor RPM",
                   ((s_sub_draft & PROTO_CLIENT_CH_MOTOR_RPM) != 0U) ? 'x' : ' ');
    (void)snprintf(lines[RC_UI_SUB_ITEM_SAVE], sizeof(lines[0]), "Save");
    (void)snprintf(lines[RC_UI_SUB_ITEM_CANCEL], sizeof(lines[0]), "Cancel");

    for (i = 0U; i < RC_UI_SUB_ITEM_COUNT; i++) {
        ptrs[i] = lines[i];
    }
    lcd_panel_show_subscribe(ptrs, RC_UI_SUB_ITEM_COUNT, s_sub_cursor);
    s_sub_dirty = FALSE;
}

static void rc_ui_subscribe_nav(const board_joystick_state_t *js1)
{
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
        if (s_sub_cursor > 0U) {
            s_sub_cursor--;
            s_sub_dirty = TRUE;
        }
        s_nav_armed = FALSE;
    } else if (js1->mapped.y_cmd <= -RC_UI_NAV_THRESH) {
        if (s_sub_cursor < (RC_UI_SUB_ITEM_COUNT - 1U)) {
            s_sub_cursor++;
            s_sub_dirty = TRUE;
        }
        s_nav_armed = FALSE;
    }
}

static void rc_ui_subscribe_activate(void)
{
    uint32_t bit;

    if (s_sub_cursor == RC_UI_SUB_ITEM_BAT) {
        bit = PROTO_CLIENT_CH_BATTERY;
        s_sub_draft ^= bit;
        s_sub_dirty = TRUE;
    } else if (s_sub_cursor == RC_UI_SUB_ITEM_US) {
        bit = PROTO_CLIENT_CH_ULTRASONIC;
        s_sub_draft ^= bit;
        s_sub_dirty = TRUE;
    } else if (s_sub_cursor == RC_UI_SUB_ITEM_RPM) {
        bit = PROTO_CLIENT_CH_MOTOR_RPM;
        s_sub_draft ^= bit;
        s_sub_dirty = TRUE;
    } else if (s_sub_cursor == RC_UI_SUB_ITEM_SAVE) {
        rc_ui_leave_subscribe(TRUE);
    } else if (s_sub_cursor == RC_UI_SUB_ITEM_CANCEL) {
        rc_ui_leave_subscribe(FALSE);
    }
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

    if ((s_mode == RC_UI_MODE_DRIVE) || (s_mode == RC_UI_MODE_SUBSCRIBE)) {
        s_throttle = 0;
        s_steer = 0;
        (void)rc_link_send_drive_stop();
        (void)rc_link_unsubscribe_optional();
        rc_link_telem_clear();
    }

    screen_target_close();
    s_mode = RC_UI_MODE_MENU;
    s_nav_armed = TRUE;
    s_tip = NULL;
    s_tip_ms = 0U;
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

static void action_switch_model(void *app_ctx, menu_engine_t *eng, const menu_item_t *item)
{
    (void)app_ctx;
    (void)eng;
    (void)item;
    rc_ui_switch_model(FALSE);
    s_menu_dirty = TRUE;
}

static void action_open_subscribe(void *app_ctx, menu_engine_t *eng, const menu_item_t *item)
{
    (void)app_ctx;
    (void)eng;
    (void)item;
    if (rc_link_up() == FALSE) {
        LOG_WARN("rc_ui: subscribe needs link");
        return;
    }
    s_sub_return_drive = FALSE;
    rc_ui_enter_subscribe();
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

/**
 * 更新 Monitor 文案；返回需要重绘的行位图 bit0..3。
 * 不触发整页 dirty——调用方对变化行做局部刷新。
 */
static uint8_t rc_ui_update_monitor_cache(const board_joystick_state_t js[BOARD_JOYSTICK_COUNT])
{
    uint16_t raw[4];
    int16_t cmd[4];
    uint8_t i;
    uint8_t dirty = 0U;

    raw[0] = js[0].raw.x_raw;
    raw[1] = js[0].raw.y_raw;
    raw[2] = js[1].raw.x_raw;
    raw[3] = js[1].raw.y_raw;
    cmd[0] = js[0].mapped.x_cmd;
    cmd[1] = js[0].mapped.y_cmd;
    cmd[2] = js[1].mapped.x_cmd;
    cmd[3] = js[1].mapped.y_cmd;

    for (i = 0U; i < 4U; i++) {
        bool_t changed;

        if (!s_mon_cache_valid) {
            changed = TRUE;
        } else {
            changed = rc_ui_delta_u16(raw[i], s_mon_raw[i], RC_UI_MON_RAW_STEP) ||
                      rc_ui_delta_i16(cmd[i], s_mon_cmd[i], RC_UI_MON_CMD_STEP);
        }
        if (!changed) {
            continue;
        }
        s_mon_raw[i] = raw[i];
        s_mon_cmd[i] = cmd[i];
        (void)snprintf(s_monitor_line[i], sizeof(s_monitor_line[i]), "%u/%d",
                       (unsigned)raw[i], (int)cmd[i]);
        dirty |= (uint8_t)(1U << i);
    }

    s_mon_cache_valid = TRUE;
    return dirty;
}

/** 仅重绘 Monitor 中数值变化的行（标题/光标/其它行不动） */
static void rc_ui_paint_monitor_rows(uint8_t dirty_mask)
{
    uint16_t focus_pos = 0U;
    uint8_t i;
    char rowbuf[36];

    if (dirty_mask == 0U) {
        return;
    }

    (void)menu_page_focus_cursor_pos(&s_monitor_page, menu_current_index(&s_menu), &focus_pos);

    for (i = 0U; i < 4U; i++) {
        if ((dirty_mask & (uint8_t)(1U << i)) == 0U) {
            continue;
        }
        (void)snprintf(rowbuf, sizeof(rowbuf), "%s %s", s_mon_labels[i], s_monitor_line[i]);
        lcd_panel_update_menu_row(i, rowbuf, (i == (uint8_t)focus_pos) ? TRUE : FALSE);
    }
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
    rc_input_reset();
    s_link_up_prev = FALSE;
    rc_ui_enter_home();
    return STATUS_OK;
}

static void rc_ui_handle_home(const rc_input_snapshot_t *in, uint32_t dt_ms)
{
    rc_link_state_t link_state = rc_link_state();
    const char *tip = NULL;
    const board_joystick_state_t *js = in->js;

    s_throttle = 0;
    s_steer = 0;

    if (screen_target_is_active() != FALSE) {
        if (in->js2_edge || in->js2_short) {
            screen_target_close();
            screen_home_show();
        } else if (in->js1_edge) {
            (void)screen_target_confirm();
            screen_target_close();
            screen_home_show();
        } else {
            screen_target_nav_js2(in->js[BOARD_JOYSTICK_2].mapped.x_cmd);
        }
        return;
    }

    if (in->js1_js2_combo_edge) {
        rc_ui_enter_menu();
        return;
    }

    if (in->js1_edge) {
        if (link_state == RC_LINK_OFF) {
            (void)rc_link_connect();
            rc_ui_set_tip("CONNECT");
        } else if (link_state == RC_LINK_CONNECTING) {
            rc_link_cancel_connect();
            rc_ui_set_tip("CANCEL");
        } else if (rc_link_up() != FALSE) {
            const rc_model_t *model = rc_model_active();

            if ((model != NULL) && (model->input_src == RC_MODEL_INPUT_IMU_TILT)) {
                if (!rc_ui_tilt_arm_ready()) {
                    rc_ui_set_tip("LEVEL");
                } else {
                    rc_ui_enter_drive();
                    return;
                }
            } else if (!rc_input_sticks_centered(js)) {
                rc_ui_set_tip("CENTER");
            } else {
                rc_ui_enter_drive();
                return;
            }
        } else {
            rc_ui_set_tip("NO LINK");
        }
    }

    if (in->js2_long) {
        screen_target_open();
        screen_target_paint();
        return;
    }

    if (in->js2_short) {
        rc_ui_switch_model(FALSE);
    }

    (void)rc_ui_tip_active(dt_ms, &tip);
    if ((tip == NULL) && rc_link_wrong_device()) {
        tip = "WRONG DEV";
    }
    if ((tip == NULL) && (link_state == RC_LINK_CONNECTING)) {
        tip = "CONNECT";
    }
    screen_home_paint(js, dt_ms, link_state, tip);
}

void rc_ui_tick(uint32_t dt_ms)
{
    rc_input_snapshot_t in;
    bool_t link_up;

    if (!rc_input_tick(dt_ms, &in)) {
        return;
    }

    link_up = rc_link_up();

    if (s_mode == RC_UI_MODE_HOME) {
        rc_ui_handle_home(&in, dt_ms);
        return;
    }

    if (s_mode == RC_UI_MODE_DRIVE) {
        const board_joystick_state_t *js = in.js;
        const rc_model_t *model = rc_model_active();
        rc_model_input_src_t src = RC_MODEL_INPUT_STICK;

        if (model != NULL) {
            src = model->input_src;
        }

        if ((s_link_up_prev != FALSE) && (link_up == FALSE)) {
            s_throttle = 0;
            s_steer = 0;
            (void)rc_link_send_drive_stop();
            LOG_WARN("rc_ui: link down (stay DRIVE, muted)");
        } else if ((s_link_up_prev == FALSE) && (link_up != FALSE)) {
            (void)rc_link_subscribe(rc_sub_get_mask());
            LOG_INFO("rc_ui: link up (resume DRIVE, resubscribe)");
        }
        s_link_up_prev = link_up;

        if (link_up == FALSE) {
            s_throttle = 0;
            s_steer = 0;
        } else {
            rc_mixer_get_drive(&s_throttle, &s_steer, src, js);
        }

        if (in.js1_edge) {
            if (rc_ui_drive_sticks_released(js)) {
                rc_ui_disarm_to_home();
                return;
            }
        }

        if (in.js2_long) {
            rc_ui_enter_menu();
            return;
        }
        if (in.js2_short) {
            rc_ui_switch_model(TRUE);
            return;
        }

        rc_ui_paint_drive();
        return;
    }

    if (s_mode == RC_UI_MODE_SUBSCRIBE) {
        if ((s_link_up_prev != FALSE) && (link_up == FALSE)) {
            LOG_WARN("rc_ui: link down (stay SUB)");
        } else if ((s_link_up_prev == FALSE) && (link_up != FALSE)) {
            LOG_INFO("rc_ui: link up (stay SUB)");
        }
        s_link_up_prev = link_up;

        s_throttle = 0;
        s_steer = 0;

        if (in.js2_short) {
            rc_ui_leave_subscribe(FALSE);
            return;
        }

        if (in.js1_edge) {
            rc_ui_subscribe_activate();
            if (s_mode != RC_UI_MODE_SUBSCRIBE) {
                return;
            }
        }

        if (in.js2_long) {
            rc_ui_enter_menu();
            return;
        }

        rc_ui_subscribe_nav(&in.js[BOARD_JOYSTICK_1]);
        if (s_sub_dirty) {
            rc_ui_render_subscribe();
        }
        return;
    }

    if (s_mode == RC_UI_MODE_CAL) {
        rc_ui_cal_tick(in.js, in.js1_edge, in.js2_edge);
        return;
    }

    /* MENU */
    if (in.js1_edge) {
        menu_result_t r = menu_dispatch(&s_menu, MENU_EVT_ENTER);
        if (r == MENU_RESULT_EXIT) {
            return;
        }
        if (s_mode == RC_UI_MODE_CAL) {
            return;
        }
        s_menu_dirty = TRUE;
    }
    if (in.js2_edge) {
        menu_result_t r = menu_dispatch(&s_menu, MENU_EVT_BACK);
        if (r == MENU_RESULT_EXIT) {
            return;
        }
        s_menu_dirty = TRUE;
    }

    rc_ui_menu_nav_from_stick(&in.js[BOARD_JOYSTICK_1]);

    if (menu_current_page(&s_menu) == &s_monitor_page) {
        uint8_t mon_dirty = rc_ui_update_monitor_cache(in.js);

        if (s_menu_dirty) {
            rc_ui_render_menu();
        } else if (mon_dirty != 0U) {
            rc_ui_paint_monitor_rows(mon_dirty);
        }
        return;
    }

    s_mon_cache_valid = FALSE;

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
    if (s_mode != RC_UI_MODE_DRIVE) {
        return TRUE;
    }
    /* 闪断期间禁发，避免 link 刚恢复前误发旧杆量 */
    if (rc_link_up() == FALSE) {
        return TRUE;
    }
    return FALSE;
}

int16_t rc_ui_last_throttle(void)
{
    return s_throttle;
}

int16_t rc_ui_last_steer(void)
{
    return s_steer;
}
