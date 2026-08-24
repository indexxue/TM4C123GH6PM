/**
 * @file    proto.h
 * @brief   UART0 蓝牙二进制协议层
 *
 * 速度环相关命令（小端）：
 * - PROTO_CMD_SET_SPEED (0x0032)：payload[0]=format；
 *   0: [motor_id u8][rpm i32]；
 *   1: [left_rpm i32][right_rpm i32]；
 *   2: [M1..M4 rpm i32 ×4]
 * - PROTO_CMD_SPEED_STOP (0x0033)：停止速度环并清零输出
 * 角度环相关命令（小端）：
 * - PROTO_CMD_SET_ANGLE (0x0034)：payload[0]=format；
 *   0: [delta_yaw i16][base_rpm i32] — 相对当前航向转角 Δθ（度）；
 *   1: [delta_yaw i16][base_rpm i32][max_turn_rpm i32]
 * - PROTO_CMD_ANGLE_STOP (0x0035)：停止角度环
 * - PROTO_CMD_CALIB_YAW (0x0036)：payload [ref_yaw i16] — 静止时将当前物理朝向设为 ref_yaw（-180~180°）
 *   ACK: [offset_deg i16][yaw_deg i16]
 * 距离环相关命令（小端）：
 * - PROTO_CMD_SET_DISTANCE (0x0037)：payload [dist_mm i32][max_rpm i32] — 相对位移 Δs（mm）
 * - PROTO_CMD_DISTANCE_STOP (0x0038)：停止距离环
 * 循迹环相关命令（小端）：
 * - PROTO_CMD_SET_LINE_FOLLOW (0x0039)：payload[0]=format；
 *   0: 无附加（用 NVS line_base_rpm）；
 *   1: [base_rpm i32]
 * - PROTO_CMD_LINE_FOLLOW_STOP (0x003A)：停止循迹环
 * 相机 / 云台（经 SPI camera_spi，小端）：
 * - PROTO_CMD_CAM_SERVO_CENTER (0x0040)：无 payload
 * - PROTO_CMD_CAM_SERVO_SET_ANGLE (0x0041)：[ch u8][deg_x100 i16]
 * - PROTO_CMD_CAM_SERVO_NUDGE (0x0042)：[ch u8][delta_x100 i16]
 * - PROTO_CMD_CAM_DETECT_ENABLE (0x0043)：[on u8]
 * - PROTO_CMD_GET_CAM_SNAPSHOT (0x0044)：一次打包 link+detect+servo
 * - PROTO_CMD_GET_CAM_NET (0x0045)：图传入口（IP/端口/path_id）
 * 订阅 PROTO_CH_CAM_DETECT (bit9) / PROTO_CH_CAM_SERVO (bit10)
 */

#ifndef PROTO_H
#define PROTO_H

#include "type.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PROTO_CMD_SET_SPEED         0x0032U
#define PROTO_CMD_SPEED_STOP        0x0033U
#define PROTO_CMD_SET_ANGLE         0x0034U
#define PROTO_CMD_ANGLE_STOP        0x0035U
#define PROTO_CMD_CALIB_YAW         0x0036U
#define PROTO_CMD_SET_DISTANCE      0x0037U
#define PROTO_CMD_DISTANCE_STOP     0x0038U
#define PROTO_CMD_SET_LINE_FOLLOW   0x0039U
#define PROTO_CMD_LINE_FOLLOW_STOP  0x003AU
#define PROTO_CMD_CAM_SERVO_CENTER  0x0040U
#define PROTO_CMD_CAM_SERVO_SET_ANGLE 0x0041U
#define PROTO_CMD_CAM_SERVO_NUDGE   0x0042U
#define PROTO_CMD_CAM_DETECT_ENABLE 0x0043U
#define PROTO_CMD_GET_CAM_SNAPSHOT  0x0044U
#define PROTO_CMD_GET_CAM_NET       0x0045U

#define PROTO_CAP_SPEED_LOOP        (1U << 4)
#define PROTO_CAP_ANGLE_LOOP        (1U << 5)
#define PROTO_CAP_YAW_CALIB         (1U << 6)
#define PROTO_CAP_DISTANCE_LOOP     (1U << 7)
#define PROTO_CAP_LINE_FOLLOW       (1U << 8)
#define PROTO_CAP_CAMERA            (1U << 9)
#define PROTO_CH_MOTOR_RPM          (1U << 5)
#define PROTO_CH_ANGLE_LOOP         (1U << 6)
#define PROTO_CH_DISTANCE_LOOP      (1U << 7)
#define PROTO_CH_CAM_DETECT         (1U << 9)
#define PROTO_CH_CAM_SERVO          (1U << 10)

status_t proto_uart_service_start(void);
void proto_telemetry_tick(uint32_t period_ms);

/**
 * 主机链路监测与主动重连（在 app 定时器上下文调用）。
 * 无主机有效命令超过阈值时，最多自动恢复 5 次；按键可强制再启一轮。
 */
void proto_link_tick(uint32_t period_ms);
void proto_link_force_reconnect(void);
bool_t proto_host_is_linked(void);

/** 原始字节回显（绕过帧解析），用于 UART0/蓝牙链路验证 */
void proto_echo_set(bool_t enable);
bool_t proto_echo_get(void);

/** 向 UART0 发送原始数据（线程安全） */
void proto_send_raw(const uint8_t *data, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif /* PROTO_H */
