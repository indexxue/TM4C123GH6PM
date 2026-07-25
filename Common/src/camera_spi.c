/**
 * @file camera_spi.c
 * @brief ESP32 Camera SPI 链路层（MCU = Slave，固定 32B）
 */

#include "camera_spi.h"

#include "board.h"
#include "bsp_spi.h"
#include "device_profile.h"
#include "log.h"

#include "FreeRTOS.h"
#include "task.h"

#include <string.h>

#define CAMERA_SPI_CRC_OFF            (30U)
#define CAMERA_SPI_LINK_DOWN_BAD      (10U)
#define CAMERA_SPI_LOG_PERIOD_MS      (10000U)
#define CAMERA_SPI_HB_PAYLOAD_LEN     (8U)

typedef struct {
    bool_t ready;
    uint8_t tx_seq;
    uint8_t tx_frame[CAMERA_SPI_FRAME_LEN];
    uint8_t rx_frame[CAMERA_SPI_FRAME_LEN];
    uint8_t last_rx_frame[CAMERA_SPI_FRAME_LEN];
    camera_spi_stats_t stats;
    uint32_t bad_streak;
    uint32_t log_elapsed_ms;
    uint16_t err_flags;
    uint8_t peer_role;
    camera_spi_link_t last_logged_link;
    bool_t wire_hint_logged;
} camera_spi_ctx_t;

static camera_spi_ctx_t s_cam;

static uint32_t camera_spi_now_ms(void)
{
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

uint16_t camera_spi_crc16(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFFU;
    size_t i;
    int b;

    if (data == NULL) {
        return 0U;
    }

    for (i = 0U; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (b = 0; b < 8; b++) {
            if ((crc & 0x8000U) != 0U) {
                crc = (uint16_t)((crc << 1) ^ 0x1021U);
            } else {
                crc = (uint16_t)(crc << 1);
            }
        }
    }
    return crc;
}

static uint16_t rd_u16_le(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t rd_u32_le(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void wr_u16_le(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFFU);
    p[1] = (uint8_t)((v >> 8) & 0xFFU);
}

static void wr_u32_le(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFU);
    p[1] = (uint8_t)((v >> 8) & 0xFFU);
    p[2] = (uint8_t)((v >> 16) & 0xFFU);
    p[3] = (uint8_t)((v >> 24) & 0xFFU);
}

static bool_t camera_spi_crc_selftest(void)
{
    static const uint8_t k_ascii[] = {
        '1', '2', '3', '4', '5', '6', '7', '8', '9'
    };
    static const uint8_t k_hb_hdr[CAMERA_SPI_CRC_OFF] = {
        0x5AU, 0xA5U, 0x01U, 0x00U, 0x01U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U
    };

    if (camera_spi_crc16(k_ascii, sizeof(k_ascii)) != 0x29B1U) {
        return FALSE;
    }
    if (camera_spi_crc16(k_hb_hdr, sizeof(k_hb_hdr)) != 0x6EB9U) {
        return FALSE;
    }
    return TRUE;
}

static void camera_spi_update_link(bool_t ok)
{
    uint32_t now = camera_spi_now_ms();

    if (ok != FALSE) {
        s_cam.bad_streak = 0U;
        s_cam.stats.last_ok_ms = now;
        s_cam.err_flags &= (uint16_t)~CAMERA_SPI_ERR_SLAVE_NOT_RESPONDING;
        s_cam.stats.link = CAMERA_SPI_LINK_OK;
        s_cam.err_flags &= (uint16_t)~CAMERA_SPI_ERR_LINK_CRC_STORM;
        return;
    }

    s_cam.bad_streak++;
    if (s_cam.bad_streak >= CAMERA_SPI_LINK_DOWN_BAD) {
        s_cam.stats.link = CAMERA_SPI_LINK_DOWN;
        s_cam.err_flags |= CAMERA_SPI_ERR_SLAVE_NOT_RESPONDING;
    } else if (s_cam.stats.link == CAMERA_SPI_LINK_OK) {
        s_cam.stats.link = CAMERA_SPI_LINK_DEGRADED;
    }
}

static void camera_spi_build_heartbeat(uint8_t *frame)
{
    uint16_t crc;

    (void)memset(frame, 0, CAMERA_SPI_FRAME_LEN);
    wr_u16_le(&frame[0], CAMERA_SPI_MAGIC);
    frame[2] = CAMERA_SPI_PROTO_VER;
    frame[3] = s_cam.tx_seq;
    frame[4] = CAMERA_SPI_MSG_HEARTBEAT;
    frame[5] = 0U;
    frame[6] = CAMERA_SPI_HB_PAYLOAD_LEN;
    frame[7] = 0U;
    wr_u32_le(&frame[8], camera_spi_now_ms());
    frame[12] = CAMERA_SPI_ROLE_MCU_SLAVE; /* role=2 */
    frame[13] = CAMERA_SPI_PROTO_VER;
    wr_u16_le(&frame[14], s_cam.err_flags);
    crc = camera_spi_crc16(frame, CAMERA_SPI_CRC_OFF);
    wr_u16_le(&frame[CAMERA_SPI_CRC_OFF], crc);
    s_cam.tx_seq++;
}

static void camera_spi_refresh_tx(void)
{
    camera_spi_build_heartbeat(s_cam.tx_frame);
    (void)bsp_spi_slave_load_tx(BOARD_SPI_CFG.base, s_cam.tx_frame);
}

static bool_t frame_is_all_val(const uint8_t *frame, uint8_t val)
{
    uint8_t i;

    for (i = 0U; i < CAMERA_SPI_FRAME_LEN; i++) {
        if (frame[i] != val) {
            return FALSE;
        }
    }
    return TRUE;
}

static bool_t frame_is_all_zero(const uint8_t *frame)
{
    return frame_is_all_val(frame, 0U);
}

static bool_t camera_spi_handle_rx(const uint8_t *frame)
{
    uint16_t magic;
    uint16_t crc_rx;
    uint16_t crc_calc;
    uint8_t len;
    uint8_t msg_id;

    (void)memcpy(s_cam.last_rx_frame, frame, CAMERA_SPI_FRAME_LEN);

    magic = rd_u16_le(&frame[0]);
    if (magic != CAMERA_SPI_MAGIC) {
        s_cam.stats.magic_err++;
        if (s_cam.wire_hint_logged == FALSE) {
            if (frame_is_all_zero(frame) != FALSE) {
                s_cam.wire_hint_logged = TRUE;
                LOG_WARN("cam_spi: RX=00 MOSI open? ESP47(MOSI)<->PA4");
            } else if (frame_is_all_val(frame, 0x5AU) != FALSE) {
                s_cam.wire_hint_logged = TRUE;
                LOG_WARN("cam_spi: RX=5A*32 (L0 leftover?). want 5A A5 HEARTBEAT Mode1");
            }
        }
        camera_spi_update_link(FALSE);
        return FALSE;
    }

    len = frame[6];
    if (len > CAMERA_SPI_PAYLOAD_MAX) {
        s_cam.stats.len_err++;
        camera_spi_update_link(FALSE);
        return FALSE;
    }

    crc_rx = rd_u16_le(&frame[CAMERA_SPI_CRC_OFF]);
    crc_calc = camera_spi_crc16(frame, CAMERA_SPI_CRC_OFF);
    if (crc_rx != crc_calc) {
        s_cam.stats.crc_err++;
        camera_spi_update_link(FALSE);
        return FALSE;
    }

    msg_id = frame[4];
    s_cam.stats.rx_ok++;
    s_cam.stats.last_msg_id = msg_id;
    s_cam.stats.last_flags = frame[5];
    camera_spi_update_link(TRUE);

    if (msg_id == CAMERA_SPI_MSG_HEARTBEAT) {
        if (len >= 8U) {
            (void)rd_u32_le(&frame[8]);
            s_cam.peer_role = frame[12];
        }
    }

    return TRUE;
}

static const char *camera_spi_link_str(camera_spi_link_t link)
{
    switch (link) {
    case CAMERA_SPI_LINK_OK:
        return "OK";
    case CAMERA_SPI_LINK_DEGRADED:
        return "DEG";
    case CAMERA_SPI_LINK_DOWN:
    default:
        return "DOWN";
    }
}

/** 链路变化立刻打；异常时每 10s 一条摘要；正常静默 */
static void camera_spi_log_periodic(uint32_t dt_ms)
{
    camera_spi_link_t link = s_cam.stats.link;
    bool_t changed = (link != s_cam.last_logged_link) ? TRUE : FALSE;

    s_cam.log_elapsed_ms += dt_ms;

    if (changed != FALSE) {
        s_cam.last_logged_link = link;
        s_cam.log_elapsed_ms = 0U;
        if (link == CAMERA_SPI_LINK_OK) {
            LOG_INFO("cam_spi link=OK peer_role=%u", (unsigned)s_cam.peer_role);
        } else {
            LOG_WARN("cam_spi link=%s ok=%lu mag=%lu crc=%lu peer_role=%u",
                     camera_spi_link_str(link),
                     (unsigned long)s_cam.stats.rx_ok,
                     (unsigned long)s_cam.stats.magic_err,
                     (unsigned long)s_cam.stats.crc_err,
                     (unsigned)s_cam.peer_role);
        }
        return;
    }

    if (link == CAMERA_SPI_LINK_OK) {
        s_cam.log_elapsed_ms = 0U;
        return;
    }

    if (s_cam.log_elapsed_ms < CAMERA_SPI_LOG_PERIOD_MS) {
        return;
    }
    s_cam.log_elapsed_ms = 0U;
    LOG_WARN("cam_spi link=%s ok=%lu mag=%lu crc=%lu peer_role=%u",
             camera_spi_link_str(link),
             (unsigned long)s_cam.stats.rx_ok,
             (unsigned long)s_cam.stats.magic_err,
             (unsigned long)s_cam.stats.crc_err,
             (unsigned)s_cam.peer_role);
}

status_t camera_spi_init(void)
{
    if (s_cam.ready != FALSE) {
        return STATUS_OK;
    }

    if (!device_profile_board_wants(DEVICE_BOARD_MASK_PERIPH)) {
        return STATUS_INVALID_STATE;
    }

    if (BOARD_SPI_CFG.role != BSP_SPI_ROLE_SLAVE) {
        LOG_ERROR("cam_spi: not slave");
        return STATUS_INVALID_STATE;
    }

    /* TM4C slave 连续多字节须 Mode1(SPH=1)；Mode0 会只吐出首字节 MISO */
    if (BOARD_SPI_CFG.mode != BSP_SPI_MODE_1) {
        LOG_ERROR("cam_spi: need Mode1 (CPOL0 CPHA1), got %d", (int)BOARD_SPI_CFG.mode);
        return STATUS_INVALID_STATE;
    }

    if (BOARD_SPI_CFG.data_bits != 8U) {
        LOG_ERROR("cam_spi: need 8bit, got %u", (unsigned)BOARD_SPI_CFG.data_bits);
        return STATUS_INVALID_STATE;
    }

    if (camera_spi_crc_selftest() == FALSE) {
        LOG_ERROR("cam_spi: CRC selftest fail");
        return STATUS_FAIL;
    }

    (void)memset(&s_cam, 0, sizeof(s_cam));
    s_cam.stats.link = CAMERA_SPI_LINK_DOWN;
    s_cam.last_logged_link = CAMERA_SPI_LINK_DOWN;

    camera_spi_build_heartbeat(s_cam.tx_frame);
    if (!bsp_spi_slave_start(BOARD_SPI_CFG.base, s_cam.tx_frame, NULL, NULL)) {
        LOG_ERROR("cam_spi: slave_start fail");
        return STATUS_FAIL;
    }

    s_cam.ready = TRUE;
    LOG_INFO("cam_spi ready Mode1 HB role=2");
    return STATUS_OK;
}

bool_t camera_spi_is_ready(void)
{
    return s_cam.ready;
}

void camera_spi_poll(void)
{
    if (s_cam.ready == FALSE) {
        return;
    }

    if (bsp_spi_slave_take_rx(BOARD_SPI_CFG.base, s_cam.rx_frame)) {
        (void)camera_spi_handle_rx(s_cam.rx_frame);
        camera_spi_refresh_tx();
    }

    if (device_profile_platform_wants(DEVICE_PLATFORM_MASK_LOG)) {
        camera_spi_log_periodic(20U);
    }
}

status_t camera_spi_get_stats(camera_spi_stats_t *out)
{
    if (out == NULL) {
        return STATUS_INVALID_ARG;
    }
    if (s_cam.ready == FALSE) {
        return STATUS_INVALID_STATE;
    }
    *out = s_cam.stats;
    return STATUS_OK;
}
