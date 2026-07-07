/**
 * @file device_profile.h
 * @brief 产品档案：board_mask / platform_mask 驱动 init 与任务子集（对齐 ESP32-S3 common）。
 *
 * 编译期通过 -DDEVICE_PRODUCT_ID=... 选择档案；默认 car-4wd full。
 */

#ifndef COMMON_DEVICE_PROFILE_H
#define COMMON_DEVICE_PROFILE_H

#include <stdbool.h>
#include <stdint.h>

#include "type.h"

/* -------------------------------------------------------------------------- */
/* 产品 ID（build.ps1 注入 -DDEVICE_PRODUCT_ID=...）                           */
/* -------------------------------------------------------------------------- */

#define DEVICE_PRODUCT_ID_CAR_4WD_FULL (1U)
#define DEVICE_PRODUCT_ID_CAR_2WD_FULL (2U)

#ifndef DEVICE_PRODUCT_ID
#define DEVICE_PRODUCT_ID DEVICE_PRODUCT_ID_CAR_4WD_FULL
#endif

/* -------------------------------------------------------------------------- */
/* 时钟源（bsp_driver/bsp_sysctl）                                                  */
/* -------------------------------------------------------------------------- */

typedef enum {
    DEVICE_CLOCK_INT_PIOSC = 0,
    DEVICE_CLOCK_MAIN_8MHZ = 1,
    DEVICE_CLOCK_MAIN_16MHZ = 2,
} device_clock_source_t;

/* -------------------------------------------------------------------------- */
/* 板级外设掩码（Start_Init → Motor/Encoder/Line/Board_Periph）               */
/* -------------------------------------------------------------------------- */

#define DEVICE_BOARD_MASK_MOTOR   (1U << 0)
#define DEVICE_BOARD_MASK_ENCODER (1U << 1)
#define DEVICE_BOARD_MASK_LINE    (1U << 2)
#define DEVICE_BOARD_MASK_PERIPH  (1U << 3)

#define DEVICE_BOARD_MASK_FULL                                                                                       \
    (DEVICE_BOARD_MASK_MOTOR | DEVICE_BOARD_MASK_ENCODER | DEVICE_BOARD_MASK_LINE | DEVICE_BOARD_MASK_PERIPH)

/* -------------------------------------------------------------------------- */
/* 平台壳层掩码（start.c / app.c：log、cmd、ota、led、button）                   */
/* -------------------------------------------------------------------------- */

#define DEVICE_PLATFORM_MASK_LOG    (1U << 0)
#define DEVICE_PLATFORM_MASK_CMD    (1U << 1)
#define DEVICE_PLATFORM_MASK_OTA    (1U << 2)
#define DEVICE_PLATFORM_MASK_LED    (1U << 3)
#define DEVICE_PLATFORM_MASK_BUTTON (1U << 4)

/* -------------------------------------------------------------------------- */
/* 产品档案                                                                    */
/* -------------------------------------------------------------------------- */

typedef struct {
    uint32_t product_id;
    const char *name;
    uint32_t board_mask;
    uint32_t platform_mask;
    device_clock_source_t clock_source;
} device_product_profile_t;

const device_product_profile_t *device_profile_product(void);

bool device_profile_board_wants(uint32_t mask);
bool device_profile_platform_wants(uint32_t mask);

#endif /* COMMON_DEVICE_PROFILE_H */
