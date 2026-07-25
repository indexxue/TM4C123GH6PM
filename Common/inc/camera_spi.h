/**
 * @file camera_spi.h
 * @brief ESP32 Camera 模组 SPI 链路（MCU = Slave，固定 32B 全双工）
 *
 * 物理层：Mode1（CPOL=0,CPHA=1）、MSB、1 MHz、32B/拍。
 * 协议见 docs/camera_spi_host_protocol_plan.md。
 *
 * L0 通路测试：默认关闭；需要时 -DCAMERA_SPI_WIRE_TEST=1。
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
} camera_spi_stats_t;

/**
 * 初始化链路：CRC 自检、装载首拍 HEARTBEAT、启动 SSI Slave。
 * 须在 Board_Periph_Init（含 bsp_spi_init）之后调用。
 */
status_t camera_spi_init(void);

bool_t camera_spi_is_ready(void);

/**
 * 周期调用（建议 20 ms）：取最近一帧、校验、刷新下一拍 HEARTBEAT TX。
 */
void camera_spi_poll(void);

status_t camera_spi_get_stats(camera_spi_stats_t *out);

/** CRC-16/CCITT-FALSE（联调自检 / 单测） */
uint16_t camera_spi_crc16(const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* MODULE_CAMERA_SPI_H */
