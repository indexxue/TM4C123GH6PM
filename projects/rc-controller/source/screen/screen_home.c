/**
 * @file    screen_home.c
 * @brief   HOME 屏：电池轮询 + 摇杆/水平仪
 */

#include "screen_home.h"

#include "battery.h"
#include "lcd_panel.h"
#include "rc_link.h"
#include "rc_mixer.h"
#include "rc_model.h"
#include "rc_target.h"

#define SCREEN_HOME_BAT_POLL_MS    500U

static uint16_t s_bat_poll_ms;
static uint8_t s_last_bat_pct = BATTERY_PERCENT_UNKNOWN;

void screen_home_show(void)
{
    s_bat_poll_ms = SCREEN_HOME_BAT_POLL_MS;
    s_last_bat_pct = BATTERY_PERCENT_UNKNOWN;
    lcd_panel_show_home();
}

void screen_home_paint(const board_joystick_state_t js[BOARD_JOYSTICK_COUNT],
                       uint32_t dt_ms,
                       rc_link_state_t link_state,
                       const char *tip)
{
    bool_t link_up;
    int16_t j1x;
    int16_t j1y;
    const rc_model_t *model = rc_model_active();
    bool_t tilt = (model != NULL) && (model->input_src == RC_MODEL_INPUT_IMU_TILT);

    if (js == NULL) {
        return;
    }

    s_bat_poll_ms = (uint16_t)(s_bat_poll_ms + dt_ms);
    if (s_bat_poll_ms >= SCREEN_HOME_BAT_POLL_MS) {
        s_bat_poll_ms = 0U;
        s_last_bat_pct = battery_get_percent();
    }

    link_up = (link_state == RC_LINK_CONNECTED) ? rc_link_up() : FALSE;

    if (tilt != FALSE) {
        int16_t roll = 0;
        int16_t pitch = 0;

        rc_mixer_get_display(&roll, &pitch);
        rc_mixer_tilt_to_stick(roll, pitch, &j1x, &j1y);
    } else {
        j1x = js[0].mapped.x_cmd;
        j1y = js[0].mapped.y_cmd;
    }

    {
        const char *model_name = (model != NULL) ? model->name : "Stick";
        const rc_target_t *target = rc_target_active();
        const char *target_name = (target != NULL) ? target->nickname : NULL;

        (void)lcd_panel_update_home(j1x, j1y, js[1].mapped.x_cmd, js[1].mapped.y_cmd,
                                    js[0].mapped.btn_pressed, js[1].mapped.btn_pressed,
                                    s_last_bat_pct, link_up, 0, 0, FALSE, model_name, target_name,
                                    tip);
    }
}
