/**
 * @file    proto_client.h
 * @brief   蓝牙协议主机侧（遥控器 → 小车）：HELLO/DRIVE/SUBSCRIBE + 遥测缓存
 *
 * 帧格式与 docs/bluetooth-protocol.md / Common/src/proto.c 一致；
 * 主机发送时 FLAGS 不含 DIR_DEVICE（bit0）。
 */

#ifndef COMMON_PROTO_CLIENT_H
#define COMMON_PROTO_CLIENT_H

#include "type.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PROTO_CLIENT_DRIVE_THROTTLE_MAX   1000
#define PROTO_CLIENT_DRIVE_STEER_MAX      1000

/** 与小车 PROTO_CH_* 对齐 */
#define PROTO_CLIENT_CH_BATTERY           (1U << 0)
#define PROTO_CLIENT_CH_ATTITUDE          (1U << 1)
#define PROTO_CLIENT_CH_ENCODER           (1U << 2)
#define PROTO_CLIENT_CH_ULTRASONIC        (1U << 4)
#define PROTO_CLIENT_CH_MOTOR_RPM         (1U << 5)

/** 遥控器 v1 默认可选订阅 */
#define PROTO_CLIENT_SUB_DEFAULT_MASK \
    (PROTO_CLIENT_CH_BATTERY | PROTO_CLIENT_CH_ULTRASONIC | PROTO_CLIENT_CH_MOTOR_RPM)

#define PROTO_CLIENT_TELEM_STALE_MS       4000U

/** HELLO ACK：1+fw16+hw4+caps4+serial16（见 docs/bluetooth-protocol.md） */
#define PROTO_CLIENT_HELLO_SERIAL_LEN     16U
#define PROTO_CLIENT_HELLO_ACK_SERIAL_OFF 25U
#define PROTO_CLIENT_HELLO_ACK_MIN_LEN    25U
#define PROTO_CLIENT_HELLO_ACK_FULL_LEN   41U

typedef struct {
    bool_t bat_valid;
    uint8_t bat_pct;
    uint16_t bat_mv;

    bool_t us_valid;
    uint16_t us_mm;

    bool_t att_valid;
    int16_t roll;
    int16_t pitch;
    int16_t yaw;

    bool_t rpm_valid;
    int32_t rpm[4];

    bool_t enc_valid;
    uint32_t enc[4];
} proto_client_telem_t;

typedef struct {
    uint32_t rx_bytes;
    uint32_t ring_drop;
    uint32_t frame_ok;
    uint32_t frame_crc_fail;
    uint32_t push_bat;
    uint32_t push_att;
    uint32_t push_enc;
    uint32_t push_us;
    uint32_t push_rpm;
    uint32_t push_other;
    uint32_t tx_frames;
    uint32_t tx_drive;
    uint32_t tx_bytes;
} proto_client_stats_t;

status_t proto_client_init(void);
status_t proto_client_send_hello(void);
/** @a target_serial 为空或全 0 → 任意设备；否则仅接受 ACK serial 匹配 */
status_t proto_client_send_hello_to(const char *target_serial);
status_t proto_client_send_ping(void);
status_t proto_client_send_drive(int16_t throttle, int16_t steer);
status_t proto_client_send_drive_stop(void);

/**
 * 下发 SUBSCRIBE：mask 含可选通道；常驻姿态/编码器位用于调 Hz。
 * payload 带默认 Hz。
 */
status_t proto_client_subscribe(uint32_t optional_mask);

/** UNSUBSCRIBE 0xFFFFFFFF：清可选通道 */
status_t proto_client_unsubscribe_optional(void);

/**
 * 遥控更新（在 tick 同周期调用）：仅在 link UP 时发送；
 * 静止发一次 STOP；运动指令限频并跳过重复值。
 */
void proto_client_drive_update(int16_t throttle, int16_t steer, bool_t muted);

/** 周期调用：RX 解析、链路保活（PING）与超时停驶 */
void proto_client_tick(uint32_t period_ms);

bool_t proto_client_link_up(void);

/** 最近一次 HELLO ACK 与 filter serial 不匹配 */
bool_t proto_client_hello_rejected(void);

/** 最近一次 HELLO ACK 中的设备 serial（无则 buf[0]='\\0'） */
void proto_client_peer_serial(char *buf, size_t buflen);

/** FALSE：仅外部调用 proto_client_send_hello / rc_link_connect 时握手 */
void proto_client_set_auto_hello(bool_t enable);
bool_t proto_client_auto_hello(void);

/** 清空遥测缓存（进控时调用） */
void proto_client_telem_clear(void);

/**
 * 读取遥测快照；超时字段 valid=FALSE（UI 显示 null）。
 */
void proto_client_telem_get(proto_client_telem_t *out);

/** 累计通信计数（诊断用） */
void proto_client_stats_get(proto_client_stats_t *out);

/**
 * UART7 输出自上次调用以来的增量速率（约 3s 周期调用）。
 * @param mode_tag  如 "DRIVE" / "IDLE"
 * @param heap_free FreeRTOS 剩余堆（字节），0=不打印
 * @param stk_words 主任务栈剩余（word），0=不打印
 */
void proto_client_stats_log_delta(const char *mode_tag, uint32_t heap_free, uint32_t stk_words);

#ifdef __cplusplus
}
#endif

#endif /* COMMON_PROTO_CLIENT_H */
