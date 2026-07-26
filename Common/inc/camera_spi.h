/**
 * @file camera_spi.h
 * @brief ESP32 Camera 模组 SPI 链路（MCU = Slave，固定 32B 全双工）
 *
 * 物理层：Mode1（CPOL=0,CPHA=1）、MSB、1 MHz、32B/拍。
 * 协议（双方公共）：docs/camera-spi-protocol.md
 *
 * L0 通路测试：默认关闭；需要时 -DCAMERA_SPI_WIRE_TEST=1。
 * L1 HEARTBEAT / L2 DETECT+SERVO 缓存 / L3 CTRL_CMD 对账：本模块实现。
 */

#ifndef MODULE_CAMERA_SPI_H
#define MODULE_CAMERA_SPI_H

#include "type.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CAMERA_SPI_FRAME_LEN           (32U)
#define CAMERA_SPI_MAGIC               (0xA55AU)
#define CAMERA_SPI_PROTO_VER           (0x01U)
#define CAMERA_SPI_PAYLOAD_MAX         (22U)
/** L0：MCU→ESP（MISO）填充字节 */
#define CAMERA_SPI_WIRE_TX_BYTE        (0xA5U)

#define CAMERA_SPI_MSG_HEARTBEAT       (0x01U)
#define CAMERA_SPI_MSG_STATUS          (0x02U)
#define CAMERA_SPI_MSG_NET_INFO        (0x03U)
#define CAMERA_SPI_MSG_DETECT          (0x10U)
#define CAMERA_SPI_MSG_SERVO           (0x20U)
#define CAMERA_SPI_MSG_CTRL_CMD        (0x30U)
#define CAMERA_SPI_MSG_CTRL_ACK        (0x31U)

#define CAMERA_SPI_FLAG_ACK_REQ        (1U << 0)
#define CAMERA_SPI_FLAG_SLAVE_HAS_CMD  (1U << 1)
#define CAMERA_SPI_FLAG_NACK           (1U << 2)
#define CAMERA_SPI_FLAG_BUSY           (1U << 3)

#define CAMERA_SPI_ROLE_CAMERA_MASTER  (1U)
#define CAMERA_SPI_ROLE_MCU_SLAVE      (2U)

#define CAMERA_SPI_ERR_LINK_CRC_STORM       (1U << 0)
#define CAMERA_SPI_ERR_SLAVE_NOT_RESPONDING (1U << 1)
#define CAMERA_SPI_ERR_DETECT_FAULT         (1U << 2)
#define CAMERA_SPI_ERR_SERVO_FAULT          (1U << 3)
#define CAMERA_SPI_ERR_CTRL_REJECTED_RECENT (1U << 4)

#define CAMERA_SPI_CTRL_DETECT_ENABLE       (0x01U)
#define CAMERA_SPI_CTRL_FOLLOW_ENABLE       (0x02U)
#define CAMERA_SPI_CTRL_SERVO_SET_ANGLE     (0x10U)
#define CAMERA_SPI_CTRL_SERVO_NUDGE         (0x11U)
#define CAMERA_SPI_CTRL_SERVO_CENTER        (0x12U)
#define CAMERA_SPI_CTRL_SERVO_SET_LIMITS    (0x13U)
#define CAMERA_SPI_CTRL_SERVO_RESET_LIMITS  (0x14U)
#define CAMERA_SPI_CTRL_SET_STREAM_MODE     (0x20U)
#define CAMERA_SPI_CTRL_GET_NET_INFO        (0x21U)

#define CAMERA_SPI_CTRL_RESULT_OK           (0U)
#define CAMERA_SPI_CTRL_RESULT_BAD_PARAM    (1U)
#define CAMERA_SPI_CTRL_RESULT_BUSY         (2U)
#define CAMERA_SPI_CTRL_RESULT_UNSUPPORTED  (3U)
#define CAMERA_SPI_CTRL_RESULT_FAILED       (4U)

#define CAMERA_SPI_NET_FLAG_HAS_IP          (1U << 0)
#define CAMERA_SPI_NET_FLAG_HTTP_UP         (1U << 1)
#define CAMERA_SPI_NET_FLAG_STREAM_READY    (1U << 2)
#define CAMERA_SPI_NET_WIFI_OFF             (0U)
#define CAMERA_SPI_NET_WIFI_STA             (1U)
#define CAMERA_SPI_NET_WIFI_SOFTAP          (2U)
#define CAMERA_SPI_STREAM_PATH_MJPG         (0U)

#define CAMERA_SPI_DETECT_BOX_MAX           (2U)
#define CAMERA_SPI_CTRL_ARGC_MAX            (18U)

typedef enum {
    CAMERA_SPI_LINK_DOWN = 0,
    CAMERA_SPI_LINK_DEGRADED,
    CAMERA_SPI_LINK_OK,
} camera_spi_link_t;

typedef struct {
    camera_spi_link_t link;
    uint32_t rx_ok;
    uint32_t crc_err;
    uint32_t magic_err;
    uint32_t len_err;
    uint8_t last_msg_id;
    uint8_t last_flags;
    uint32_t last_ok_ms;
    uint8_t peer_role;
    uint16_t detect_rx;
    uint16_t servo_rx;
    uint16_t net_rx;
    uint16_t ctrl_ack_ok;
    uint16_t ctrl_ack_fail;
    uint16_t ctrl_timeout;
} camera_spi_stats_t;

typedef struct {
    uint16_t x;
    uint16_t y;
    uint16_t w;
    uint8_t score_u8;
    uint8_t class_id;
} camera_spi_box_t;

typedef struct {
    bool_t valid;
    uint16_t frame_w;
    uint16_t frame_h;
    uint8_t count;
    uint8_t best_index;
    camera_spi_box_t box[CAMERA_SPI_DETECT_BOX_MAX];
    uint32_t rx_ms;
} camera_spi_detect_t;

typedef struct {
    bool_t valid;
    int16_t pan_deg_x100;
    int16_t tilt_deg_x100;
    uint16_t pan_pulse_us;
    uint16_t tilt_pulse_us;
    int16_t pan_min_x100;
    int16_t pan_max_x100;
    int16_t tilt_min_x100;
    int16_t tilt_max_x100;
    uint32_t rx_ms;
} camera_spi_servo_t;

typedef struct {
    bool_t valid;
    uint32_t ipv4;          /* a.b.c.d → a|(b<<8)|(c<<16)|(d<<24) */
    uint16_t http_port;
    uint8_t wifi_mode;      /* 0=OFF 1=STA 2=SoftAP */
    uint8_t flags;          /* HAS_IP / HTTP_UP / STREAM_READY */
    uint8_t stream_path_id; /* 0=/api/camera/stream.mjpg */
    uint32_t rx_ms;
} camera_spi_net_info_t;

typedef enum {
    CAMERA_SPI_CTRL_IDLE = 0,
    CAMERA_SPI_CTRL_PENDING,
    CAMERA_SPI_CTRL_DONE,
    CAMERA_SPI_CTRL_TIMEOUT,
} camera_spi_ctrl_state_t;

typedef struct {
    camera_spi_ctrl_state_t state;
    uint8_t sub_cmd;
    uint8_t req_id;
    uint8_t result;
    uint32_t detail;
} camera_spi_ctrl_status_t;

/**
 * 初始化链路：CRC 自检、装载首拍 HEARTBEAT、启动 SSI Slave。
 * 须在 Board_Periph_Init（含 bsp_spi_init）之后调用。
 */
status_t camera_spi_init(void);

bool_t camera_spi_is_ready(void);

/**
 * 周期调用（建议 20 ms）：取最近一帧、校验、刷新下一拍 TX（HB / CTRL_CMD）。
 */
void camera_spi_poll(void);

status_t camera_spi_get_stats(camera_spi_stats_t *out);

status_t camera_spi_get_detect(camera_spi_detect_t *out);

status_t camera_spi_get_servo(camera_spi_servo_t *out);

status_t camera_spi_get_net_info(camera_spi_net_info_t *out);

status_t camera_spi_get_ctrl_status(camera_spi_ctrl_status_t *out);

/**
 * 投递 CTRL_CMD（L3）。链路非 OK 或已有 pending 时失败。
 * @param args  sub_cmd 头之后的参数字节（可为 NULL 当 argc=0）
 */
status_t camera_spi_ctrl_send(uint8_t sub_cmd, const uint8_t *args, uint8_t argc);

status_t camera_spi_ctrl_detect_enable(uint8_t on);
status_t camera_spi_ctrl_servo_center(void);
status_t camera_spi_ctrl_servo_set_angle(uint8_t ch, int16_t deg_x100);
status_t camera_spi_ctrl_servo_nudge(uint8_t ch, int16_t delta_x100);
status_t camera_spi_request_net_info(void);

/** CRC-16/CCITT-FALSE（联调自检 / 单测） */
uint16_t camera_spi_crc16(const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* MODULE_CAMERA_SPI_H */
