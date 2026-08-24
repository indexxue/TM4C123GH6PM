/**
 * @file    joy_cal.c
 * @brief   摇杆校准 NVS blob（ns=cal key=joy）
 */

#include "joy_cal.h"

#include "joystick.h"
#include "log.h"
#include "nvs.h"

#include "crc32.h"

#include <string.h>

#define JOY_CAL_NVS_NS     NVS_CFG_NS_CAL
#define JOY_CAL_NVS_KEY    "joy"

static joy_cal_t s_cal;
static bool_t s_ready;

static void joy_cal_fill_crc(joy_cal_t *cal)
{
    joy_cal_t tmp;

    if (cal == NULL) {
        return;
    }
    tmp = *cal;
    tmp.crc32 = 0U;
    cal->crc32 = crc32_compute(&tmp, sizeof(tmp));
}

void joy_cal_get_default(joy_cal_t *out)
{
    size_t i;

    if (out == NULL) {
        return;
    }
    (void)memset(out, 0, sizeof(*out));
    out->magic = JOY_CAL_MAGIC;
    out->version = JOY_CAL_VERSION;
    for (i = 0U; i < JOY_CAL_AXIS_COUNT; i++) {
        out->axis[i].min = BOARD_JOY_ADC_MIN;
        out->axis[i].center = BOARD_JOY_ADC_CENTER;
        out->axis[i].max = BOARD_JOY_ADC_MAX;
    }
    out->deadband = JOY_CAL_DEADBAND_DEF;
    out->invert_mask = 0U;
    joy_cal_fill_crc(out);
}

bool_t joy_cal_validate(const joy_cal_t *cal)
{
    joy_cal_t tmp;
    uint32_t crc;
    size_t i;

    if (cal == NULL) {
        return FALSE;
    }
    if ((cal->magic != JOY_CAL_MAGIC) || (cal->version != JOY_CAL_VERSION)) {
        return FALSE;
    }
    if (cal->deadband > 800U) {
        return FALSE;
    }

    tmp = *cal;
    tmp.crc32 = 0U;
    crc = crc32_compute(&tmp, sizeof(tmp));
    if (crc != cal->crc32) {
        return FALSE;
    }

    for (i = 0U; i < JOY_CAL_AXIS_COUNT; i++) {
        if ((cal->axis[i].min >= cal->axis[i].center) ||
            (cal->axis[i].center >= cal->axis[i].max)) {
            return FALSE;
        }
        if (cal->axis[i].max > BOARD_JOY_ADC_MAX) {
            return FALSE;
        }
    }
    return TRUE;
}

static void joy_cal_apply_to_joystick(const joy_cal_t *cal)
{
    size_t i;
    board_joystick_axis_cfg_t cfg;

    if (cal == NULL) {
        return;
    }

    for (i = 0U; i < JOY_CAL_AXIS_COUNT; i++) {
        board_joystick_axis_cfg_default(&cfg);
        cfg.adc_min = cal->axis[i].min;
        cfg.adc_center = cal->axis[i].center;
        cfg.adc_max = cal->axis[i].max;
        cfg.deadband = cal->deadband;
        /* 校准后的 min/max 已是实测极限，不再额外扣边缘 */
        cfg.edge_margin = 0U;
        cfg.invert = ((cal->invert_mask & (uint8_t)(1U << i)) != 0U) ? TRUE : FALSE;
        board_joystick_set_axis_cfg((board_joystick_axis_id_t)i, &cfg);
    }
}

status_t joy_cal_init(void)
{
    joy_cal_t loaded;
    uint32_t len = (uint32_t)sizeof(loaded);
    status_t st;

    joy_cal_get_default(&s_cal);

    st = nvs_get_blob(JOY_CAL_NVS_NS, JOY_CAL_NVS_KEY, &loaded, &len);
    if ((st == STATUS_OK) && (len == (uint32_t)sizeof(loaded)) && joy_cal_validate(&loaded)) {
        s_cal = loaded;
        LOG_INFO("joy_cal: loaded from nvs");
    } else {
        LOG_INFO("joy_cal: using defaults");
    }

    joy_cal_apply_to_joystick(&s_cal);
    s_ready = TRUE;
    return STATUS_OK;
}

const joy_cal_t *joy_cal_get(void)
{
    return &s_cal;
}

status_t joy_cal_save(const joy_cal_t *cal)
{
    joy_cal_t tmp;

    if ((cal == NULL) || !s_ready) {
        return STATUS_FAIL;
    }

    tmp = *cal;
    tmp.magic = JOY_CAL_MAGIC;
    tmp.version = JOY_CAL_VERSION;
    joy_cal_fill_crc(&tmp);
    if (!joy_cal_validate(&tmp)) {
        LOG_WARN("joy_cal: save rejected (invalid)");
        return STATUS_FAIL;
    }

    if (nvs_set_blob(JOY_CAL_NVS_NS, JOY_CAL_NVS_KEY, &tmp, (uint32_t)sizeof(tmp)) != STATUS_OK) {
        LOG_WARN("joy_cal: nvs write fail");
        return STATUS_FAIL;
    }

    s_cal = tmp;
    joy_cal_apply_to_joystick(&s_cal);
    LOG_INFO("joy_cal: saved");
    return STATUS_OK;
}

status_t joy_cal_restore_default(void)
{
    joy_cal_t def;

    joy_cal_get_default(&def);
    return joy_cal_save(&def);
}

status_t joy_cal_set_deadband(uint16_t deadband)
{
    joy_cal_t tmp = s_cal;

    tmp.deadband = deadband;
    return joy_cal_save(&tmp);
}

status_t joy_cal_toggle_invert(joy_cal_axis_id_t axis)
{
    joy_cal_t tmp = s_cal;

    if ((uint32_t)axis >= JOY_CAL_AXIS_COUNT) {
        return STATUS_FAIL;
    }
    tmp.invert_mask ^= (uint8_t)(1U << (uint8_t)axis);
    return joy_cal_save(&tmp);
}

bool_t joy_cal_axis_inverted(joy_cal_axis_id_t axis)
{
    if ((uint32_t)axis >= JOY_CAL_AXIS_COUNT) {
        return FALSE;
    }
    return ((s_cal.invert_mask & (uint8_t)(1U << (uint8_t)axis)) != 0U) ? TRUE : FALSE;
}
