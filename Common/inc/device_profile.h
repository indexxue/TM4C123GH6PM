/**
 * @file device_profile.h
 * @brief 产品档案：board_mask / platform_mask 驱动 init 与任务子集（对齐 ESP32-S3 common）。
 *
 * 编译期通过 -DDEVICE_PRODUCT_ID=... 选择档案；默认 car-4wd full。
 * 厂测槽（-DFLASH_FACTORY_SLOT）在对应车型档案上叠加 CMD，板级仍跟车型 .syscfg。
 */

#ifndef COMMON_DEVICE_PROFILE_H
#define COMMON_DEVICE_PROFILE_H

#include <stdbool.h>
#include <stdint.h>

#include "type.h"

/* -------------------------------------------------------------------------- */
/* 产品 ID（build.ps1 注入 -DDEVICE_PRODUCT_ID=...）                           */
/* -------------------------------------------------------------------------- */

/** 兼容旧厂测独立工程；新构建优先 car-4wd/car-2wd -Target factory */
#define DEVICE_PRODUCT_ID_FACTORY      (0U)
#define DEVICE_PRODUCT_ID_CAR_4WD_FULL (1U)
#define DEVICE_PRODUCT_ID_CAR_2WD_FULL (2U)
#define DEVICE_PRODUCT_ID_RC_CONTROLLER (3U)

#ifndef DEVICE_PRODUCT_ID
#define DEVICE_PRODUCT_ID DEVICE_PRODUCT_ID_CAR_4WD_FULL
#endif

/* 构建脚本注入：产品名 / 构建时间戳（未注入时用占位） */
#ifndef FW_PRODUCT_NAME
#define FW_PRODUCT_NAME "unknown"
#endif
#ifndef FW_BUILD_DATE
#define FW_BUILD_DATE "0000-00-00"
#endif
#ifndef FW_BUILD_TIME
#define FW_BUILD_TIME "00:00:00"
#endif

/* -------------------------------------------------------------------------- */
/* 时钟源（bsp_driver/bsp_sysctl）                                                  */
/* -------------------------------------------------------------------------- */

typedef enum {
    DEVICE_CLOCK_INT_PIOSC = 0,
    DEVICE_CLOCK_MAIN_8MHZ = 1,
} device_clock_source_t;

/* -------------------------------------------------------------------------- */
/* 板级外设掩码（Start_Init → Motor/Encoder/Line/Board_Periph）               */
/* -------------------------------------------------------------------------- */

#define DEVICE_BOARD_MASK_MOTOR   (1U << 0)
#define DEVICE_BOARD_MASK_ENCODER (1U << 1)
#define DEVICE_BOARD_MASK_LINE    (1U << 2)
#define DEVICE_BOARD_MASK_PERIPH  (1U << 3)
/** I2C 传感器（IMU/磁力计/姿态） */
#define DEVICE_BOARD_MASK_IMU     (1U << 4)
#define DEVICE_BOARD_MASK_MAG     (1U << 5)
#define DEVICE_BOARD_MASK_LED     (1U << 6)
#define DEVICE_BOARD_MASK_BUZZER  (1U << 7)
#define DEVICE_BOARD_MASK_ULTRA   (1U << 8)
#define DEVICE_BOARD_MASK_BATTERY (1U << 9)
/** SSD1306 OLED（I2C0 @ 0x3C） */
#define DEVICE_BOARD_MASK_OLED    (1U << 10)
/** ST7789 LCD（SPI + GPIO CS） */
#define DEVICE_BOARD_MASK_LCD     (1U << 11)
/** NRF24 2.4G（SPI + GPIO CS） */
#define DEVICE_BOARD_MASK_NRF24   (1U << 12)
/** 操纵杆 ADC + 按键 */
#define DEVICE_BOARD_MASK_JOYSTICK (1U << 13)

#define DEVICE_BOARD_MASK_SENSORS (DEVICE_BOARD_MASK_IMU | DEVICE_BOARD_MASK_MAG)

#define DEVICE_BOARD_MASK_FULL                                                                         \
    (DEVICE_BOARD_MASK_MOTOR | DEVICE_BOARD_MASK_ENCODER | DEVICE_BOARD_MASK_LINE |                   \
     DEVICE_BOARD_MASK_PERIPH | DEVICE_BOARD_MASK_SENSORS | DEVICE_BOARD_MASK_LED |                    \
     DEVICE_BOARD_MASK_BUZZER | DEVICE_BOARD_MASK_ULTRA | DEVICE_BOARD_MASK_BATTERY |                 \
     DEVICE_BOARD_MASK_OLED)

/* -------------------------------------------------------------------------- */
/* 平台壳层掩码（start.c / app.c：log、cmd、button）                   */
/* -------------------------------------------------------------------------- */

#define DEVICE_PLATFORM_MASK_LOG    (1U << 0)
#define DEVICE_PLATFORM_MASK_CMD    (1U << 1)
#define DEVICE_PLATFORM_MASK_BUTTON (1U << 2)

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

/** 当前镜像是否为厂测槽（APP_B / FLASH_FACTORY_SLOT） */
bool device_profile_is_factory_slot(void);

#endif /* COMMON_DEVICE_PROFILE_H */
