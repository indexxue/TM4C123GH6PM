/**
 * @file camera_spi.c
 * @brief ESP32 Camera SPI 链路层（MCU = Slave，固定 32B）
 *
 * L1：HEARTBEAT role=2
 * L2：解析/缓存 DETECT_RESULT、SERVO_TELEMETRY
 * L3：SLAVE_HAS_CMD → CTRL_CMD → 对账 CTRL_ACK（超时重发）
 */

#include "camera_spi.h"

#include "board.h"
#include "bsp_spi.h"
#include "device_profile.h"
#include "log.h"

#include "FreeRTOS.h"
#include "task.h"

#include <string.h>

/*
 * L0 通路测试：1=固定 TX=A5×32，只统计 RX 图案，不校 magic/CRC。
 * 打通后改为 0（或 -DCAMERA_SPI_WIRE_TEST=0）恢复 HEARTBEAT。
 */
#ifndef CAMERA_SPI_WIRE_TEST
#define CAMERA_SPI_WIRE_TEST          (0)
#endif

#define CAMERA_SPI_CRC_OFF            (30U)
#define CAMERA_SPI_LINK_DOWN_BAD      (10U)
#if CAMERA_SPI_WIRE_TEST
#define CAMERA_SPI_LOG_PERIOD_MS      (2000U)
#else
#define CAMERA_SPI_LOG_PERIOD_MS      (10000U)
#endif
#define CAMERA_SPI_HB_PAYLOAD_LEN     (8U)
#define CAMERA_SPI_CTRL_ACK_TIMEOUT_MS (100U)
#define CAMERA_SPI_CTRL_MAX_RETRY     (5U)
#define CAMERA_SPI_L2_LOG_PERIOD_MS   (1000U)

typedef enum {
    CTRL_PHASE_IDLE = 0,
    CTRL_PHASE_ADVERTISE,
    CTRL_PHASE_EMIT,
    CTRL_PHASE_WAIT_ACK,
} ctrl_phase_t;

typedef struct {
    bool_t ready;
    uint8_t tx_seq;
    uint8_t tx_frame[CAMERA_SPI_FRAME_LEN];
    uint8_t rx_frame[CAMERA_SPI_FRAME_LEN];
    uint8_t last_rx_frame[CAMERA_SPI_FRAME_LEN];
    camera_spi_stats_t stats;
    uint32_t bad_streak;
    uint32_t log_elapsed_ms;
    uint32_t l2_log_elapsed_ms;
    uint16_t err_flags;
    uint8_t peer_role;
    camera_spi_link_t last_logged_link;
    bool_t wire_hint_logged;

    camera_spi_detect_t detect;
    camera_spi_servo_t servo;
    camera_spi_net_info_t net;

    ctrl_phase_t ctrl_phase;
    camera_spi_ctrl_state_t ctrl_state;
    uint8_t ctrl_sub_cmd;
    uint8_t ctrl_req_id;
    uint8_t ctrl_argc;
    uint8_t ctrl_args[CAMERA_SPI_CTRL_ARGC_MAX];
    uint8_t ctrl_result;
    uint32_t ctrl_detail;
    uint8_t ctrl_next_req_id;
    uint8_t ctrl_retry;
    uint32_t ctrl_deadline_ms;
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

#if CAMERA_SPI_WIRE_TEST
static void camera_spi_build_wire_tx(uint8_t *frame)
{
    (void)memset(frame, CAMERA_SPI_WIRE_TX_BYTE, CAMERA_SPI_FRAME_LEN);
}

static uint8_t frame_count_byte(const uint8_t *frame, uint8_t val)
{
    uint8_t n = 0U;
    uint8_t i;

    for (i = 0U; i < CAMERA_SPI_FRAME_LEN; i++) {
        if (frame[i] == val) {
            n++;
        }
    }
    return n;
}

static bool_t camera_spi_handle_rx_wire(const uint8_t *frame)
{
    uint8_t n5a;
    uint8_t n00;
    uint8_t nff;
    bool_t mosi_ok;

    (void)memcpy(s_cam.last_rx_frame, frame, CAMERA_SPI_FRAME_LEN);

    n5a = frame_count_byte(frame, 0x5AU);
    n00 = frame_count_byte(frame, 0x00U);
    nff = frame_count_byte(frame, 0xFFU);

    mosi_ok = FALSE;
    if ((frame[0] == 0x5AU) && (frame[1] == 0xA5U)) {
        mosi_ok = TRUE;
    } else if (n5a >= 16U) {
        mosi_ok = TRUE;
    } else if ((n00 < 28U) && (nff < 28U)) {
        s_cam.stats.magic_err++;
        camera_spi_update_link(FALSE);
        return FALSE;
    }

    if (mosi_ok != FALSE) {
        s_cam.stats.rx_ok++;
        camera_spi_update_link(TRUE);
        return TRUE;
    }

    s_cam.stats.magic_err++;
    camera_spi_update_link(FALSE);
    return FALSE;
}
#else
static uint16_t rd_u16_le(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static int16_t rd_i16_le(const uint8_t *p)
{
    return (int16_t)rd_u16_le(p);
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

static void camera_spi_frame_hdr(uint8_t *frame, uint8_t msg_id, uint8_t flags, uint8_t len)
{
    (void)memset(frame, 0, CAMERA_SPI_FRAME_LEN);
    wr_u16_le(&frame[0], CAMERA_SPI_MAGIC);
    frame[2] = CAMERA_SPI_PROTO_VER;
    frame[3] = s_cam.tx_seq;
    frame[4] = msg_id;
    frame[5] = flags;
    frame[6] = len;
    frame[7] = 0U;
}

static void camera_spi_frame_seal(uint8_t *frame)
{
    uint16_t crc = camera_spi_crc16(frame, CAMERA_SPI_CRC_OFF);
    wr_u16_le(&frame[CAMERA_SPI_CRC_OFF], crc);
    s_cam.tx_seq++;
}

static void camera_spi_build_heartbeat(uint8_t *frame, uint8_t flags)
{
    camera_spi_frame_hdr(frame, CAMERA_SPI_MSG_HEARTBEAT, flags, CAMERA_SPI_HB_PAYLOAD_LEN);
    wr_u32_le(&frame[8], camera_spi_now_ms());
    frame[12] = CAMERA_SPI_ROLE_MCU_SLAVE;
    frame[13] = CAMERA_SPI_PROTO_VER;
    wr_u16_le(&frame[14], s_cam.err_flags);
    camera_spi_frame_seal(frame);
}

static void camera_spi_build_ctrl_cmd(uint8_t *frame)
{
    uint8_t len = (uint8_t)(4U + s_cam.ctrl_argc);

    camera_spi_frame_hdr(frame, CAMERA_SPI_MSG_CTRL_CMD, 0U, len);
    frame[8] = s_cam.ctrl_sub_cmd;
    frame[9] = s_cam.ctrl_req_id;
    frame[10] = s_cam.ctrl_argc;
    frame[11] = 0U;
    if (s_cam.ctrl_argc > 0U) {
        (void)memcpy(&frame[12], s_cam.ctrl_args, s_cam.ctrl_argc);
    }
    camera_spi_frame_seal(frame);
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

static void camera_spi_handle_detect(const uint8_t *payload, uint8_t len)
{
    uint8_t count;
    uint8_t i;
    uint8_t need;

    if (len < 6U) {
        return;
    }

    count = payload[4];
    if (count > CAMERA_SPI_DETECT_BOX_MAX) {
        count = CAMERA_SPI_DETECT_BOX_MAX;
    }
    need = (uint8_t)(6U + (uint8_t)(count * 8U));
    if (len < need) {
        return;
    }

    s_cam.detect.valid = TRUE;
    s_cam.detect.frame_w = rd_u16_le(&payload[0]);
    s_cam.detect.frame_h = rd_u16_le(&payload[2]);
    s_cam.detect.count = count;
    s_cam.detect.best_index = payload[5];
    s_cam.detect.rx_ms = camera_spi_now_ms();
    (void)memset(s_cam.detect.box, 0, sizeof(s_cam.detect.box));

    for (i = 0U; i < count; i++) {
        const uint8_t *b = &payload[6U + (uint8_t)(i * 8U)];
        s_cam.detect.box[i].x = rd_u16_le(&b[0]);
        s_cam.detect.box[i].y = rd_u16_le(&b[2]);
        s_cam.detect.box[i].w = rd_u16_le(&b[4]);
        s_cam.detect.box[i].score_u8 = b[6];
        s_cam.detect.box[i].class_id = b[7];
    }

    s_cam.stats.detect_rx++;
}

static void camera_spi_handle_servo(const uint8_t *payload, uint8_t len)
{
    if (len < 16U) {
        return;
    }

    s_cam.servo.valid = TRUE;
    s_cam.servo.pan_deg_x100 = rd_i16_le(&payload[0]);
    s_cam.servo.tilt_deg_x100 = rd_i16_le(&payload[2]);
    s_cam.servo.pan_pulse_us = rd_u16_le(&payload[4]);
    s_cam.servo.tilt_pulse_us = rd_u16_le(&payload[6]);
    s_cam.servo.pan_min_x100 = rd_i16_le(&payload[8]);
    s_cam.servo.pan_max_x100 = rd_i16_le(&payload[10]);
    s_cam.servo.tilt_min_x100 = rd_i16_le(&payload[12]);
    s_cam.servo.tilt_max_x100 = rd_i16_le(&payload[14]);
    s_cam.servo.rx_ms = camera_spi_now_ms();
    s_cam.stats.servo_rx++;
}

static void camera_spi_handle_net_info(const uint8_t *payload, uint8_t len)
{
    if (len < 10U) {
        return;
    }

    s_cam.net.valid = TRUE;
    s_cam.net.ipv4 = rd_u32_le(&payload[0]);
    s_cam.net.http_port = rd_u16_le(&payload[4]);
    s_cam.net.wifi_mode = payload[6];
    s_cam.net.flags = payload[7];
    s_cam.net.stream_path_id = payload[8];
    s_cam.net.rx_ms = camera_spi_now_ms();
    s_cam.stats.net_rx++;
}

static void camera_spi_handle_ctrl_ack(const uint8_t *payload, uint8_t len)
{
    uint8_t sub_cmd;
    uint8_t req_id;
    uint8_t result;

    if (len < 4U) {
        return;
    }
    if (s_cam.ctrl_phase != CTRL_PHASE_WAIT_ACK) {
        return;
    }

    sub_cmd = payload[0];
    req_id = payload[1];
    result = payload[2];
    if ((sub_cmd != s_cam.ctrl_sub_cmd) || (req_id != s_cam.ctrl_req_id)) {
        return;
    }

    s_cam.ctrl_result = result;
    s_cam.ctrl_detail = (len >= 8U) ? rd_u32_le(&payload[4]) : 0U;
    s_cam.ctrl_phase = CTRL_PHASE_IDLE;
    s_cam.ctrl_state = CAMERA_SPI_CTRL_DONE;

    if (result == CAMERA_SPI_CTRL_RESULT_OK) {
        s_cam.stats.ctrl_ack_ok++;
        s_cam.err_flags &= (uint16_t)~CAMERA_SPI_ERR_CTRL_REJECTED_RECENT;
        LOG_INFO("cam_spi CTRL_ACK ok sub=0x%02X req=%u",
                 (unsigned)sub_cmd, (unsigned)req_id);
    } else {
        s_cam.stats.ctrl_ack_fail++;
        s_cam.err_flags |= CAMERA_SPI_ERR_CTRL_REJECTED_RECENT;
        LOG_WARN("cam_spi CTRL_ACK fail sub=0x%02X req=%u result=%u detail=%lu",
                 (unsigned)sub_cmd, (unsigned)req_id, (unsigned)result,
                 (unsigned long)s_cam.ctrl_detail);
    }
}

static void camera_spi_ctrl_on_exchange_done(void)
{
    if (s_cam.ctrl_phase == CTRL_PHASE_ADVERTISE) {
        /* 上一拍 MISO 已带 SLAVE_HAS_CMD，本拍装 CTRL_CMD */
        s_cam.ctrl_phase = CTRL_PHASE_EMIT;
    } else if (s_cam.ctrl_phase == CTRL_PHASE_EMIT) {
        s_cam.ctrl_phase = CTRL_PHASE_WAIT_ACK;
        s_cam.ctrl_deadline_ms = camera_spi_now_ms() + CAMERA_SPI_CTRL_ACK_TIMEOUT_MS;
    }
}

static void camera_spi_ctrl_tick(void)
{
    uint32_t now;

    if (s_cam.ctrl_phase != CTRL_PHASE_WAIT_ACK) {
        return;
    }

    now = camera_spi_now_ms();
    if ((int32_t)(now - s_cam.ctrl_deadline_ms) < 0) {
        return;
    }

    if (s_cam.ctrl_retry < CAMERA_SPI_CTRL_MAX_RETRY) {
        s_cam.ctrl_retry++;
        s_cam.ctrl_phase = CTRL_PHASE_ADVERTISE;
        LOG_WARN("cam_spi CTRL retry %u/%u sub=0x%02X req=%u",
                 (unsigned)s_cam.ctrl_retry, (unsigned)CAMERA_SPI_CTRL_MAX_RETRY,
                 (unsigned)s_cam.ctrl_sub_cmd, (unsigned)s_cam.ctrl_req_id);
        return;
    }

    s_cam.ctrl_phase = CTRL_PHASE_IDLE;
    s_cam.ctrl_state = CAMERA_SPI_CTRL_TIMEOUT;
    s_cam.stats.ctrl_timeout++;
    s_cam.err_flags |= CAMERA_SPI_ERR_CTRL_REJECTED_RECENT;
    LOG_WARN("cam_spi CTRL timeout sub=0x%02X req=%u",
             (unsigned)s_cam.ctrl_sub_cmd, (unsigned)s_cam.ctrl_req_id);
}

static void camera_spi_dispatch_msg(uint8_t msg_id, const uint8_t *payload, uint8_t len)
{
    switch (msg_id) {
    case CAMERA_SPI_MSG_HEARTBEAT:
        if (len >= 8U) {
            (void)rd_u32_le(&payload[0]);
            s_cam.peer_role = payload[4];
        }
        break;
    case CAMERA_SPI_MSG_STATUS:
        /* 计数由本端维护；STATUS 仅确认对端在线 */
        break;
    case CAMERA_SPI_MSG_NET_INFO:
        camera_spi_handle_net_info(payload, len);
        break;
    case CAMERA_SPI_MSG_DETECT:
        camera_spi_handle_detect(payload, len);
        break;
    case CAMERA_SPI_MSG_SERVO:
        camera_spi_handle_servo(payload, len);
        break;
    case CAMERA_SPI_MSG_CTRL_ACK:
        camera_spi_handle_ctrl_ack(payload, len);
        break;
    default:
        break;
    }
}
#endif

static void camera_spi_refresh_tx(void)
{
#if CAMERA_SPI_WIRE_TEST
    camera_spi_build_wire_tx(s_cam.tx_frame);
#else
    if (s_cam.ctrl_phase == CTRL_PHASE_EMIT) {
        camera_spi_build_ctrl_cmd(s_cam.tx_frame);
    } else if (s_cam.ctrl_phase == CTRL_PHASE_ADVERTISE) {
        camera_spi_build_heartbeat(s_cam.tx_frame, CAMERA_SPI_FLAG_SLAVE_HAS_CMD);
    } else {
        camera_spi_build_heartbeat(s_cam.tx_frame, 0U);
    }
#endif
    (void)bsp_spi_slave_load_tx(BOARD_SPI_CFG.base, s_cam.tx_frame);
}

#if CAMERA_SPI_WIRE_TEST
static void hex32(char *out, size_t out_len, const uint8_t *frame)
{
    static const char k_hex[] = "0123456789ABCDEF";
    size_t i;
    size_t o = 0U;

    if ((out == NULL) || (out_len < ((CAMERA_SPI_FRAME_LEN * 3U) + 1U))) {
        if ((out != NULL) && (out_len > 0U)) {
            out[0] = '\0';
        }
        return;
    }

    for (i = 0U; i < CAMERA_SPI_FRAME_LEN; i++) {
        if (i != 0U) {
            out[o++] = ' ';
        }
        out[o++] = k_hex[(frame[i] >> 4) & 0x0FU];
        out[o++] = k_hex[frame[i] & 0x0FU];
    }
    out[o] = '\0';
}
#endif

static bool_t camera_spi_handle_rx(const uint8_t *frame)
{
#if CAMERA_SPI_WIRE_TEST
    return camera_spi_handle_rx_wire(frame);
#else
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
                LOG_WARN("cam_spi: RX=00 MOSI open? ESP45(MOSI)<->PA4");
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
    camera_spi_dispatch_msg(msg_id, &frame[8], len);
    return TRUE;
#endif
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

#if !CAMERA_SPI_WIRE_TEST
static void camera_spi_log_l2(uint32_t dt_ms)
{
    s_cam.l2_log_elapsed_ms += dt_ms;
    if (s_cam.l2_log_elapsed_ms < CAMERA_SPI_L2_LOG_PERIOD_MS) {
        return;
    }
    s_cam.l2_log_elapsed_ms = 0U;

    if (s_cam.stats.link != CAMERA_SPI_LINK_OK) {
        return;
    }

    if (s_cam.detect.valid != FALSE) {
        const camera_spi_box_t *b0 = &s_cam.detect.box[0];
        LOG_INFO("cam_spi DETECT n=%u best=%u %ux%u box0=(%u,%u,w=%u sc=%u cls=%u)",
                 (unsigned)s_cam.detect.count,
                 (unsigned)s_cam.detect.best_index,
                 (unsigned)s_cam.detect.frame_w,
                 (unsigned)s_cam.detect.frame_h,
                 (unsigned)b0->x, (unsigned)b0->y, (unsigned)b0->w,
                 (unsigned)b0->score_u8, (unsigned)b0->class_id);
    }

    if (s_cam.servo.valid != FALSE) {
        LOG_INFO("cam_spi SERVO pan=%d tilt=%d pulse=%u/%u",
                 (int)s_cam.servo.pan_deg_x100, (int)s_cam.servo.tilt_deg_x100,
                 (unsigned)s_cam.servo.pan_pulse_us,
                 (unsigned)s_cam.servo.tilt_pulse_us);
    }
}
#endif

static void camera_spi_log_periodic(uint32_t dt_ms)
{
    bsp_spi_slave_diag_t diag;
    char rx_hex[(CAMERA_SPI_FRAME_LEN * 3U) + 1U];

    s_cam.log_elapsed_ms += dt_ms;
    if (s_cam.log_elapsed_ms < CAMERA_SPI_LOG_PERIOD_MS) {
        return;
    }
    s_cam.log_elapsed_ms = 0U;

#if CAMERA_SPI_WIRE_TEST
    {
        uint8_t n5a = frame_count_byte(s_cam.last_rx_frame, 0x5AU);
        uint8_t n00 = frame_count_byte(s_cam.last_rx_frame, 0x00U);
        uint8_t nff = frame_count_byte(s_cam.last_rx_frame, 0xFFU);
        const char *mosi;

        (void)memset(&diag, 0, sizeof(diag));
        (void)bsp_spi_slave_get_diag(BOARD_SPI_CFG.base, &diag);
        hex32(rx_hex, sizeof(rx_hex), s_cam.last_rx_frame);

        if ((s_cam.last_rx_frame[0] == 0x5AU) && (s_cam.last_rx_frame[1] == 0xA5U)) {
            mosi = "OK(HB)";
        } else if (n5a >= 16U) {
            mosi = "OK(5A*)";
        } else if ((n00 >= 28U) || (nff >= 28U)) {
            mosi = "FAIL(idle)";
        } else {
            mosi = "WEAK";
        }

        LOG_INFO("cam_spi L0: MOSI=%s link=%s ok=%lu bad=%lu frm=%lu underrun=%lu "
                 "n5a=%u n00=%u nff=%u",
                 mosi,
                 camera_spi_link_str(s_cam.stats.link),
                 (unsigned long)s_cam.stats.rx_ok,
                 (unsigned long)s_cam.stats.magic_err,
                 (unsigned long)diag.frames_done,
                 (unsigned long)diag.tx_underrun,
                 (unsigned)n5a, (unsigned)n00, (unsigned)nff);
        LOG_INFO("cam_spi L0: RX32 %s", rx_hex);
        LOG_INFO("cam_spi L0: TX=A5x32  ESP应见 RX A5 A5 A5 A5...");
    }
#else
    {
        camera_spi_link_t link = s_cam.stats.link;
        bool_t changed = (link != s_cam.last_logged_link) ? TRUE : FALSE;

        if (changed != FALSE) {
            s_cam.last_logged_link = link;
            if (link == CAMERA_SPI_LINK_OK) {
                LOG_INFO("cam_spi link=OK peer_role=%u det=%u servo=%u",
                         (unsigned)s_cam.peer_role,
                         (unsigned)s_cam.stats.detect_rx,
                         (unsigned)s_cam.stats.servo_rx);
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
            return;
        }

        LOG_WARN("cam_spi link=%s ok=%lu mag=%lu crc=%lu peer_role=%u",
                 camera_spi_link_str(link),
                 (unsigned long)s_cam.stats.rx_ok,
                 (unsigned long)s_cam.stats.magic_err,
                 (unsigned long)s_cam.stats.crc_err,
                 (unsigned)s_cam.peer_role);
        (void)diag;
        (void)rx_hex;
    }
#endif
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
    s_cam.ctrl_next_req_id = 1U;
    s_cam.ctrl_state = CAMERA_SPI_CTRL_IDLE;
    s_cam.ctrl_phase = CTRL_PHASE_IDLE;

#if CAMERA_SPI_WIRE_TEST
    camera_spi_build_wire_tx(s_cam.tx_frame);
#else
    camera_spi_build_heartbeat(s_cam.tx_frame, 0U);
#endif
    if (!bsp_spi_slave_start(BOARD_SPI_CFG.base, s_cam.tx_frame, NULL, NULL)) {
        LOG_ERROR("cam_spi: slave_start fail");
        return STATUS_FAIL;
    }

    s_cam.ready = TRUE;
#if CAMERA_SPI_WIRE_TEST
    LOG_INFO("cam_spi L0 WIRE_TEST Mode1 TX=A5x32");
#else
    LOG_INFO("cam_spi ready Mode1 HB role=2 L2/L3");
#endif
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
        if (camera_spi_handle_rx(s_cam.rx_frame) != FALSE) {
#if !CAMERA_SPI_WIRE_TEST
            camera_spi_ctrl_on_exchange_done();
#endif
        }
        camera_spi_refresh_tx();
    }

#if !CAMERA_SPI_WIRE_TEST
    camera_spi_ctrl_tick();
#endif

    if (device_profile_platform_wants(DEVICE_PLATFORM_MASK_LOG)) {
        camera_spi_log_periodic(20U);
#if !CAMERA_SPI_WIRE_TEST
        camera_spi_log_l2(20U);
#endif
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
    out->peer_role = s_cam.peer_role;
    return STATUS_OK;
}

status_t camera_spi_get_detect(camera_spi_detect_t *out)
{
    if (out == NULL) {
        return STATUS_INVALID_ARG;
    }
    if (s_cam.ready == FALSE) {
        return STATUS_INVALID_STATE;
    }
    *out = s_cam.detect;
    return STATUS_OK;
}

status_t camera_spi_get_servo(camera_spi_servo_t *out)
{
    if (out == NULL) {
        return STATUS_INVALID_ARG;
    }
    if (s_cam.ready == FALSE) {
        return STATUS_INVALID_STATE;
    }
    *out = s_cam.servo;
    return STATUS_OK;
}

status_t camera_spi_get_net_info(camera_spi_net_info_t *out)
{
    if (out == NULL) {
        return STATUS_INVALID_ARG;
    }
    if (s_cam.ready == FALSE) {
        return STATUS_INVALID_STATE;
    }
    *out = s_cam.net;
    return STATUS_OK;
}

status_t camera_spi_get_ctrl_status(camera_spi_ctrl_status_t *out)
{
    if (out == NULL) {
        return STATUS_INVALID_ARG;
    }
    if (s_cam.ready == FALSE) {
        return STATUS_INVALID_STATE;
    }
    out->state = s_cam.ctrl_state;
    out->sub_cmd = s_cam.ctrl_sub_cmd;
    out->req_id = s_cam.ctrl_req_id;
    out->result = s_cam.ctrl_result;
    out->detail = s_cam.ctrl_detail;
    return STATUS_OK;
}

status_t camera_spi_ctrl_send(uint8_t sub_cmd, const uint8_t *args, uint8_t argc)
{
    if (s_cam.ready == FALSE) {
        return STATUS_INVALID_STATE;
    }
#if CAMERA_SPI_WIRE_TEST
    (void)sub_cmd;
    (void)args;
    (void)argc;
    return STATUS_NOT_SUPPORTED;
#else
    if (argc > CAMERA_SPI_CTRL_ARGC_MAX) {
        return STATUS_INVALID_ARG;
    }
    if ((argc > 0U) && (args == NULL)) {
        return STATUS_INVALID_ARG;
    }
    if (s_cam.stats.link != CAMERA_SPI_LINK_OK) {
        return STATUS_INVALID_STATE;
    }
    if (s_cam.ctrl_phase != CTRL_PHASE_IDLE) {
        return STATUS_INVALID_STATE;
    }

    s_cam.ctrl_sub_cmd = sub_cmd;
    s_cam.ctrl_req_id = s_cam.ctrl_next_req_id;
    if (s_cam.ctrl_next_req_id == 0xFFU) {
        s_cam.ctrl_next_req_id = 1U;
    } else {
        s_cam.ctrl_next_req_id++;
    }
    s_cam.ctrl_argc = argc;
    if (argc > 0U) {
        (void)memcpy(s_cam.ctrl_args, args, argc);
    }
    s_cam.ctrl_result = 0U;
    s_cam.ctrl_detail = 0U;
    s_cam.ctrl_retry = 0U;
    s_cam.ctrl_state = CAMERA_SPI_CTRL_PENDING;
    s_cam.ctrl_phase = CTRL_PHASE_ADVERTISE;

    /* 立即预装带 SLAVE_HAS_CMD 的心跳，供下一拍 CS 前发出 */
    camera_spi_refresh_tx();

    LOG_INFO("cam_spi CTRL_CMD queue sub=0x%02X req=%u argc=%u",
             (unsigned)sub_cmd, (unsigned)s_cam.ctrl_req_id, (unsigned)argc);
    return STATUS_OK;
#endif
}

status_t camera_spi_ctrl_detect_enable(uint8_t on)
{
    uint8_t arg = (on != 0U) ? 1U : 0U;
    return camera_spi_ctrl_send(CAMERA_SPI_CTRL_DETECT_ENABLE, &arg, 1U);
}

status_t camera_spi_ctrl_servo_center(void)
{
    return camera_spi_ctrl_send(CAMERA_SPI_CTRL_SERVO_CENTER, NULL, 0U);
}

status_t camera_spi_ctrl_servo_set_angle(uint8_t ch, int16_t deg_x100)
{
    uint8_t args[3];

    args[0] = ch;
    args[1] = (uint8_t)((uint16_t)deg_x100 & 0xFFU);
    args[2] = (uint8_t)(((uint16_t)deg_x100 >> 8) & 0xFFU);
    return camera_spi_ctrl_send(CAMERA_SPI_CTRL_SERVO_SET_ANGLE, args, 3U);
}

status_t camera_spi_ctrl_servo_nudge(uint8_t ch, int16_t delta_x100)
{
    uint8_t args[3];

    args[0] = ch;
    args[1] = (uint8_t)((uint16_t)delta_x100 & 0xFFU);
    args[2] = (uint8_t)(((uint16_t)delta_x100 >> 8) & 0xFFU);
    return camera_spi_ctrl_send(CAMERA_SPI_CTRL_SERVO_NUDGE, args, 3U);
}

status_t camera_spi_request_net_info(void)
{
    return camera_spi_ctrl_send(CAMERA_SPI_CTRL_GET_NET_INFO, NULL, 0U);
}
