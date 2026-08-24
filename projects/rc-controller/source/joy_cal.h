/**
 * @file    joy_cal.h
 * @brief   双摇杆校准表：NVS 持久化 + 应用到 board/joystick 运行时 cfg
 *
 * 见 docs/rc-joystick-menu-design.md
 */

#ifndef RC_JOY_CAL_H
#define RC_JOY_CAL_H

#include "type.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define JOY_CAL_AXIS_COUNT     4U
#define JOY_CAL_MAGIC          0x4A43U /* 'J''C' */
#define JOY_CAL_VERSION        1U
#define JOY_CAL_DEADBAND_DEF   120U

typedef enum {
    JOY_CAL_AXIS_J1X = 0,
    JOY_CAL_AXIS_J1Y = 1,
    JOY_CAL_AXIS_J2X = 2,
    JOY_CAL_AXIS_J2Y = 3,
} joy_cal_axis_id_t;

typedef struct {
    uint16_t min;
    uint16_t center;
    uint16_t max;
} joy_cal_axis_t;

typedef struct {
    uint16_t magic;
    uint16_t version;
    joy_cal_axis_t axis[JOY_CAL_AXIS_COUNT];
    uint16_t deadband;
    uint8_t invert_mask;
    uint8_t reserved;
    uint32_t crc32;
} joy_cal_t;

/** 上电：nvs_init 之后调用；加载失败则默认并 apply */
status_t joy_cal_init(void);

const joy_cal_t *joy_cal_get(void);

void joy_cal_get_default(joy_cal_t *out);
bool_t joy_cal_validate(const joy_cal_t *cal);

/** 写入 RAM + 应用到摇杆 + 落盘 NVS */
status_t joy_cal_save(const joy_cal_t *cal);

/** 恢复固件默认并落盘 */
status_t joy_cal_restore_default(void);

/** 仅更新死区并落盘 */
status_t joy_cal_set_deadband(uint16_t deadband);

/** 翻转某一轴 invert 位并落盘 */
status_t joy_cal_toggle_invert(joy_cal_axis_id_t axis);

bool_t joy_cal_axis_inverted(joy_cal_axis_id_t axis);

#ifdef __cplusplus
}
#endif

#endif /* RC_JOY_CAL_H */
