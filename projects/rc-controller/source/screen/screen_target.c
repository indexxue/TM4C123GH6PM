/**
 * @file    screen_target.c
 * @brief   目标设备槽位选择 overlay（NVS rc/targets）
 */

#include "screen_target.h"

#include "lcd_panel.h"
#include "rc_target.h"

#include <stdio.h>
#include <string.h>

#define SCREEN_TARGET_LINE_MAX    4U
#define SCREEN_TARGET_LINE_CHARS  28U
#define SCREEN_TARGET_NAV_THRESH  450

static bool_t s_active;
static uint8_t s_cursor;
static char s_lines[SCREEN_TARGET_LINE_MAX][SCREEN_TARGET_LINE_CHARS];
static const char *s_line_ptrs[SCREEN_TARGET_LINE_MAX];
static int8_t s_nav_arm;

static void screen_target_build_lines(void)
{
    uint8_t i;
    uint8_t count = rc_target_count();

    if (count > SCREEN_TARGET_LINE_MAX) {
        count = SCREEN_TARGET_LINE_MAX;
    }
    for (i = 0U; i < count; i++) {
        const rc_target_t *t = rc_target_get(i);
        const char *nick = (t != NULL) ? t->nickname : "?";

        if ((t != NULL) && (t->serial[0] != '\0')) {
            (void)snprintf(s_lines[i], sizeof(s_lines[i]), "%s *", nick);
        } else {
            (void)snprintf(s_lines[i], sizeof(s_lines[i]), "%s", nick);
        }
        s_line_ptrs[i] = s_lines[i];
    }
    for (; i < SCREEN_TARGET_LINE_MAX; i++) {
        s_lines[i][0] = '\0';
        s_line_ptrs[i] = NULL;
    }
}

static uint8_t screen_target_vis_cursor(void)
{
    uint8_t count = rc_target_count();

    if (count == 0U) {
        return 0U;
    }
    if (count > SCREEN_TARGET_LINE_MAX) {
        count = SCREEN_TARGET_LINE_MAX;
    }
    if (s_cursor >= count) {
        s_cursor = (uint8_t)(count - 1U);
    }
    return s_cursor;
}

bool_t screen_target_is_active(void)
{
    return s_active;
}

void screen_target_open(void)
{
    s_active = TRUE;
    s_cursor = rc_target_active_index();
    s_nav_arm = 0;
    screen_target_build_lines();
    lcd_panel_show_menu("Target", s_line_ptrs, rc_target_count(), screen_target_vis_cursor(),
                        "JS1:ok  JS2:back");
}

void screen_target_close(void)
{
    s_active = FALSE;
    s_nav_arm = 0;
}

void screen_target_paint(void)
{
    if (s_active == FALSE) {
        return;
    }
    screen_target_build_lines();
    lcd_panel_show_menu("Target", s_line_ptrs, rc_target_count(), screen_target_vis_cursor(),
                        "JS1:ok  JS2:back");
}

void screen_target_nav_js2(int16_t js2_x_cmd)
{
    uint8_t count = rc_target_count();
    int8_t dir = 0;

    if ((s_active == FALSE) || (count <= 1U)) {
        return;
    }
    if (count > SCREEN_TARGET_LINE_MAX) {
        count = SCREEN_TARGET_LINE_MAX;
    }

    if (js2_x_cmd >= SCREEN_TARGET_NAV_THRESH) {
        dir = 1;
    } else if (js2_x_cmd <= (int16_t)(-SCREEN_TARGET_NAV_THRESH)) {
        dir = -1;
    } else {
        s_nav_arm = 0;
        return;
    }

    if (s_nav_arm == dir) {
        return;
    }
    s_nav_arm = dir;

    if (dir > 0) {
        s_cursor = (uint8_t)((s_cursor + 1U) % count);
    } else {
        s_cursor = (s_cursor == 0U) ? (uint8_t)(count - 1U) : (uint8_t)(s_cursor - 1U);
    }
    screen_target_paint();
}

status_t screen_target_confirm(void)
{
    if (s_active == FALSE) {
        return STATUS_FAIL;
    }
    return rc_target_set_active(s_cursor);
}
