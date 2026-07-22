/**
 * @file    led_scene.h
 * @brief   LED 场景驱动（WS2812B @ board.h RGB_LED，如 car-4wd PC3）
 */

#ifndef MODULE_LED_SCENE_H
#define MODULE_LED_SCENE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

#define LED_SCENE_ACTION_NUM    3U
#define LED_SCENE_MSEC          1U
#define CYCLE_ALWAYS            0xFFFFU
#define LED_SCENE_LED_NUM       1U

typedef enum
{
    LED_SCENE_LED_0 = 0,
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
    LED_SCENE_ID_BOOTUP = 0,
    LED_SCENE_ID_PAIRING,
    LED_SCENE_ID_TRIGGER,
    LED_SCENE_ID_ERROR,
    LED_SCENE_ID_SUCCESS,
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

void led_scene_init(void);
void led_scene_update(void);
void led_scene_run(led_scene_id_e id);
void led_scene_cancel(led_scene_id_e id);
void led_scene_led_direct_set(led_scene_led_e led, bool on);
/** 直接写 RGB（厂测/调试；会取消当前场景输出） */
void led_scene_rgb_set(uint8_t r, uint8_t g, uint8_t b);
/** 是否有场景正在运行（全部结束后为 false，可停止轮询 update） */
bool led_scene_is_active(void);

#ifdef __cplusplus
}
#endif

#endif /* MODULE_LED_SCENE_H */
