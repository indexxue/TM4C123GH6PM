/**
 * @file    led_scene.h
 * @brief   LED 场景（与 STM32 版 API 一致）；底层输出为 WS2812B。
 */

#ifndef COMMON_LED_SCENE_H
#define COMMON_LED_SCENE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "type.h"

/** 单场景内最大动作步数（多段配色/闪烁序列） */
#define LED_SCENE_ACTION_NUM    8
#define LED_SCENE_MSEC          1
#define CYCLE_ALWAYS            0xFFFF
#define LED_SCENE_LED_NUM       2

/** 与 STM32 一致：每帧调用间隔（ms），用于 `led_scene_update` */
#define LED_SCENE_TICK_MS       50

typedef enum
{
    LED_SCENE_LED_0 = 0,
    LED_SCENE_LED_1,
    LED_SCENE_LED_MAX_NUM,
} led_scene_led_e;

typedef enum
{
    LED_SCENE_PRIO_FACTORY = 0,
    LED_SCENE_PRIO_PAIR,
    LED_SCENE_PRIO_NORMAL,
    LED_SCENE_PRIO_LOW,
    LED_SCENE_PRIO_MAX_NUM,
} led_scene_prio_e;

typedef enum
{
    LED_SCENE_ID_BOOTUP = 0,       /**< 上电自检 */
    LED_SCENE_ID_PAIRING,        /**< 配网中 */
    LED_SCENE_ID_TRIGGER,        /**< 触发：黄灯闪 5 次 */
    LED_SCENE_ID_ERROR,          /**< 异常：红灯快闪（至取消） */
    LED_SCENE_ID_SUCCESS,        /**< 配网成功：绿灯常亮约 5s */
    LED_SCENE_ID_NET_OFFLINE,    /**< 断网：红 / 蓝慢闪（至取消） */
    LED_SCENE_ID_WORKING,        /**< 工作中：彩虹渐变（至取消） */
    LED_SCENE_ID_ALARM,          /**< 报警：红灯快闪（至取消） */
    LED_SCENE_ID_NET_ONLINE,     /**< 回网：绿灯慢闪约 10s */
    LED_SCENE_ID_CONFIG,         /**< 配置态：紫灯常亮（至取消） */
    LED_SCENE_ID_CHARGING,       /**< 充电：黄灯常亮约 3s */
    LED_SCENE_ID_LOW_BATTERY,    /**< 低电：黄灯约每 60s 闪一次（至取消） */
    LED_SCENE_ID_MAX_NUM,
} led_scene_id_e;

typedef struct
{
    uint8_t r;
    uint8_t g;
    uint8_t b;
} led_rgb_value_t;

typedef enum
{
    ACTION_ONOFF = 0,
    ACTION_FADE,
    ACTION_HOLD,     /**< 常亮，直至 `led_scene_cancel` 或更高优先级场景抢占 */
    ACTION_RAINBOW, /**< 整条 WS2812 彩虹渐变，直至取消 */
} led_action_type_e;

typedef struct
{
    uint8_t cycle;
    led_action_type_e type;
    union
    {
        struct
        {
            led_rgb_value_t value;
            uint32_t lifetime;
        } onoff;
        struct
        {
            led_rgb_value_t start_value;
            led_rgb_value_t end_value;
            uint8_t step;
            uint32_t interval;
        } fade;
        struct
        {
            led_rgb_value_t value;
        } hold;
        struct
        {
            uint8_t hue_step;    /**< 每 tick 增加色相（0–255 环） */
            uint8_t saturation; /**< 0–255 */
            uint8_t value;       /**< 亮度 0–255 */
        } rainbow;
    } sub;
} led_scene_action_t;

typedef struct
{
    uint16_t cycle;
    uint8_t num;
    led_scene_action_t action[LED_SCENE_ACTION_NUM];
} led_scene_t;

typedef struct
{
    led_scene_prio_e prio;
    const led_scene_t *scene;
} led_scene_tab_t;

/**
 * @brief 按板级配置初始化 WS2812B 并启用场景模块（GPIO/颗数在 `led_scene.c` 中配置）。
 */
status_t led_scene_init(void);

/**
 * @brief 创建后台任务，按 LED_SCENE_TICK_MS 周期调用 `led_scene_update`（仅可成功调用一次）。
 */
status_t led_scene_start_update_task(void);

void led_scene_update(void);
void led_scene_run(led_scene_id_e id);
void led_scene_cancel(led_scene_id_e id);
void led_scene_led_direct_set(led_scene_led_e led, bool on);

#ifdef __cplusplus
}
#endif

#endif /* COMMON_LED_SCENE_H */
