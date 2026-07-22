/**
 * @file    boot_slot.h
 * @brief   运行时切换 Boot NVS slot（量产按键 / 厂测命令共用）
 */

#ifndef MODULE_BOOT_SLOT_H
#define MODULE_BOOT_SLOT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "type.h"

/**
 * 校验目标槽镜像、写 NVS slot、停电机并软件复位。
 * 成功时不返回；失败返回 STATUS_*。
 */
status_t boot_slot_switch(uint32_t slot);

#ifdef __cplusplus
}
#endif

#endif /* MODULE_BOOT_SLOT_H */
