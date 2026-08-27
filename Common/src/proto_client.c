/**
 * @file    proto_client.c
 * @brief   遥控器协议主机：HELLO / DRIVE / SUBSCRIBE + TELEMETRY_PUSH 解析
 */

#include "proto_client.h"

#include "board.h"
#include "log.h"

#include "bsp_uart.h"
#include "bsp_systick.h"

#include "driverlib/uart.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define PC_SOF0                 0x54U
#define PC_SOF1                 0x4DU
#define PC_VER                  0x02U
#define PC_HEADER_SIZE          7U
#define PC_MAX_PAYLOAD          256U
#define PC_MAX_FRAME            (2U + PC_HEADER_SIZE + PC_MAX_PAYLOAD + 2U)

#define PC_FLAG_DIR_DEVICE      0x01U
#define PC_FLAG_NAK             0x02U
#define PC_FLAG_UNSOLICITED     0x04U

#define PC_CMD_HELLO            0x0001U
#define PC_CMD_PING             0x0002U
#define PC_CMD_SUBSCRIBE        0x0011U
#define PC_CMD_UNSUBSCRIBE      0x0012U
#define PC_CMD_DRIVE            0x0030U
#define PC_CMD_DRIVE_STOP       0x0031U
#define PC_CMD_TELEMETRY_PUSH   0x8011U

#define PC_PUSH_CH_BATTERY      0U
#define PC_PUSH_CH_ATTITUDE     1U
#define PC_PUSH_CH_ENCODER      2U
#define PC_PUSH_CH_ULTRASONIC   4U
#define PC_PUSH_CH_MOTOR_RPM    5U

#define PC_PING_PERIOD_MS       1000U
#define PC_HELLO_RETRY_MS       1000U
#define PC_LINK_TIMEOUT_MS      8000U
#define PC_DRIVE_IDLE_STOP_MS   500U
#define PC_DRIVE_MIN_PERIOD_MS  80U
#define PC_DRIVE_HOLD_PERIOD_MS 250U
#define PC_LINK_LOG_PERIOD_MS   5000U

#define PC_HZ_ATT               5U
#define PC_HZ_ENC               5U
#define PC_HZ_ULTRA             5U
#define PC_HZ_RPM               10U

#define PC_RX_RING_SIZE         256U

typedef enum {
    PC_RX_SOF0 = 0,
    PC_RX_SOF1,
    PC_RX_HDR,
    PC_RX_PAYLOAD,
    PC_RX_CRC
} pc_rx_state_t;

static uint8_t s_seq;
static bool_t s_link_up;
static uint32_t s_last_rx_ms;
static uint32_t s_last_ping_ms;
static uint32_t s_last_hello_ms;
static uint32_t s_last_drive_ms;
static uint32_t s_last_link_log_ms;
static uint32_t s_rx_byte_count;
static int16_t s_last_sent_throttle;
static int16_t s_last_sent_steer;
static bool_t s_drive_session;
static bool_t s_auto_hello = TRUE;
static char s_hello_filter[PROTO_CLIENT_HELLO_SERIAL_LEN];
static char s_peer_serial[PROTO_CLIENT_HELLO_SERIAL_LEN];
static bool_t s_peer_valid;
static bool_t s_hello_rejected;

static pc_rx_state_t s_rx_state;
static uint8_t s_rx_hdr[PC_HEADER_SIZE];
static uint8_t s_rx_payload[PC_MAX_PAYLOAD];
static uint8_t s_rx_crc_buf[2];
static uint16_t s_rx_hdr_n;
static uint16_t s_rx_pay_n;
static uint16_t s_rx_pay_len;
static uint16_t s_rx_crc_n;
/** CRC 拼包缓冲（静态，避免在 RX 路径上开 263B 栈帧） */
static uint8_t s_crc_body[PC_HEADER_SIZE + PC_MAX_PAYLOAD];

static uint8_t s_rx_ring[PC_RX_RING_SIZE];
static volatile uint16_t s_rx_ring_head;
static volatile uint16_t s_rx_ring_tail;
static uint32_t s_rx_drop_count;

static uint8_t s_bat_pct;
static uint16_t s_bat_mv;
static uint32_t s_bat_ms;
static bool_t s_bat_have;

static uint16_t s_us_mm;
static uint32_t s_us_ms;
static bool_t s_us_have;

static int16_t s_roll;
static int16_t s_pitch;
static int16_t s_yaw;
static uint32_t s_att_ms;
static bool_t s_att_have;

static int32_t s_rpm[4];
static uint32_t s_rpm_ms;
static bool_t s_rpm_have;

static uint32_t s_enc[4];
static uint32_t s_enc_ms;
static bool_t s_enc_have;

static uint32_t s_frame_ok;
static uint32_t s_frame_crc_fail;
static uint32_t s_push_bat;
static uint32_t s_push_att;
static uint32_t s_push_enc;
static uint32_t s_push_us;
static uint32_t s_push_rpm;
static uint32_t s_push_other;
static uint32_t s_tx_frames;
static uint32_t s_tx_drive;
static uint32_t s_tx_bytes;

static proto_client_stats_t s_stats_last_log;

static uint32_t proto_client_uptime_ms(void);
static void proto_client_rx_pump(void);
static void proto_client_rx_byte(uint8_t b);

static uint32_t proto_client_uptime_ms(void)
{
    return bsp_get_tick_ms();
}

static void proto_client_put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFFU);
    p[1] = (uint8_t)((v >> 8) & 0xFFU);
}

static void proto_client_put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFU);
    p[1] = (uint8_t)((v >> 8) & 0xFFU);
    p[2] = (uint8_t)((v >> 16) & 0xFFU);
    p[3] = (uint8_t)((v >> 24) & 0xFFU);
}

static uint16_t proto_client_get_u16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t proto_client_get_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static int16_t proto_client_get_i16(const uint8_t *p)
{
    return (int16_t)proto_client_get_u16(p);
}

static int32_t proto_client_get_i32(const uint8_t *p)
{
    return (int32_t)proto_client_get_u32(p);
}

static uint16_t proto_client_crc16(const uint8_t *data, uint16_t len)
{
    uint16_t crc = 0xFFFFU;
    uint16_t i;
    uint8_t bit;

    for (i = 0U; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (bit = 0U; bit < 8U; bit++) {
            if ((crc & 0x8000U) != 0U) {
                crc = (uint16_t)((crc << 1) ^ 0x1021U);
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

static bool_t proto_client_build_frame(uint8_t *frame, uint16_t *total_out, uint16_t cmd,
                                       const uint8_t *payload, uint16_t len)
{
    uint8_t *body;
    uint16_t crc;
    uint16_t total;

    if ((frame == NULL) || (total_out == NULL) || (len > PC_MAX_PAYLOAD)) {
        return FALSE;
    }

    frame[0] = PC_SOF0;
    frame[1] = PC_SOF1;
    body = &frame[2];
    body[0] = PC_VER;
    body[1] = 0U;
    proto_client_put_u16(&body[2], len);
    proto_client_put_u16(&body[4], cmd);
    body[6] = s_seq++;
    if ((len > 0U) && (payload != NULL)) {
        (void)memcpy(&body[7], payload, len);
    }

    crc = proto_client_crc16(body, (uint16_t)(PC_HEADER_SIZE + len));
    proto_client_put_u16(&body[7 + len], crc);
    total = (uint16_t)(2U + PC_HEADER_SIZE + len + 2U);
    *total_out = total;
    return TRUE;
}

static bool_t proto_client_send(uint16_t cmd, const uint8_t *payload, uint16_t len)
{
    uint8_t frame[PC_MAX_FRAME];
    uint16_t total;
    uint16_t i;
    uint32_t base = BOARD_UART_BT_CFG.base;

    if (!proto_client_build_frame(frame, &total, cmd, payload, len)) {
        return FALSE;
    }

    s_tx_frames++;
    s_tx_bytes += (uint32_t)total;
    if (cmd == PC_CMD_DRIVE) {
        s_tx_drive++;
    }

    /* 半双工：发前先抽空 RX，避免 FIFO 在 TX 阻塞期间溢出 */
    proto_client_rx_pump();

    for (i = 0U; i < total; i++) {
        UARTCharPut(base, frame[i]);
        if ((i & 0x07U) == 0x07U) {
            proto_client_rx_pump();
        }
    }
    while (UARTBusy(base)) {
        proto_client_rx_pump();
    }
    proto_client_rx_pump();
    return TRUE;
}

static void proto_client_rx_ring_push(uint8_t b)
{
    uint16_t next = (uint16_t)((s_rx_ring_head + 1U) % PC_RX_RING_SIZE);

    if (next == s_rx_ring_tail) {
        s_rx_drop_count++;
        return;
    }
    s_rx_ring[s_rx_ring_head] = b;
    s_rx_ring_head = next;
}

static void proto_client_rx_pump(void)
{
    uint32_t base = BOARD_UART_BT_CFG.base;
    int32_t ch;

    while (UARTCharsAvail(base)) {
        ch = UARTCharGetNonBlocking(base);
        if (ch < 0) {
            break;
        }
        proto_client_rx_ring_push((uint8_t)ch);
    }
}

static void proto_client_note_rx(void)
{
    s_last_rx_ms = proto_client_uptime_ms();
    if (s_link_up == FALSE) {
        s_link_up = TRUE;
        LOG_INFO("proto_client: link UP (rx=%lu B)", (unsigned long)s_rx_byte_count);
    }
}

static void proto_client_handle_push(const uint8_t *payload, uint16_t len)
{
    uint8_t ch;
    uint32_t now;

    if ((payload == NULL) || (len < 5U)) {
        return;
    }

    ch = payload[0];
    now = proto_client_uptime_ms();

    if ((ch == PC_PUSH_CH_BATTERY) && (len >= 8U)) {
        s_bat_mv = proto_client_get_u16(&payload[5]);
        s_bat_pct = payload[7];
        s_bat_ms = now;
        s_bat_have = TRUE;
        s_push_bat++;
    } else if ((ch == PC_PUSH_CH_ATTITUDE) && (len >= 11U)) {
        s_roll = proto_client_get_i16(&payload[5]);
        s_pitch = proto_client_get_i16(&payload[7]);
        s_yaw = proto_client_get_i16(&payload[9]);
        s_att_ms = now;
        s_att_have = TRUE;
        s_push_att++;
    } else if ((ch == PC_PUSH_CH_ENCODER) && (len >= 21U)) {
        s_enc[0] = proto_client_get_u32(&payload[5]);
        s_enc[1] = proto_client_get_u32(&payload[9]);
        s_enc[2] = proto_client_get_u32(&payload[13]);
        s_enc[3] = proto_client_get_u32(&payload[17]);
        s_enc_ms = now;
        s_enc_have = TRUE;
        s_push_enc++;
    } else if ((ch == PC_PUSH_CH_ULTRASONIC) && (len >= 7U)) {
        s_us_mm = proto_client_get_u16(&payload[5]);
        s_us_ms = now;
        s_us_have = TRUE;
        s_push_us++;
    } else if ((ch == PC_PUSH_CH_MOTOR_RPM) && (len >= 21U)) {
        s_rpm[0] = proto_client_get_i32(&payload[5]);
        s_rpm[1] = proto_client_get_i32(&payload[9]);
        s_rpm[2] = proto_client_get_i32(&payload[13]);
        s_rpm[3] = proto_client_get_i32(&payload[17]);
        s_rpm_ms = now;
        s_rpm_have = TRUE;
        s_push_rpm++;
    } else {
        s_push_other++;
    }
}

static bool_t proto_client_serial_broadcast(const char *serial)
{
    size_t i;

    if (serial == NULL) {
        return TRUE;
    }
    for (i = 0U; i < PROTO_CLIENT_HELLO_SERIAL_LEN; i++) {
        if (serial[i] != '\0') {
            return FALSE;
        }
    }
    return TRUE;
}

static void proto_client_handle_hello_ack(const uint8_t *payload, uint16_t len)
{
    s_peer_valid = FALSE;
    s_peer_serial[0] = '\0';
    s_hello_rejected = FALSE;

    if ((payload == NULL) || (len < PROTO_CLIENT_HELLO_ACK_MIN_LEN)) {
        return;
    }

    if (len >= PROTO_CLIENT_HELLO_ACK_FULL_LEN) {
        (void)memcpy(s_peer_serial, &payload[PROTO_CLIENT_HELLO_ACK_SERIAL_OFF],
                     PROTO_CLIENT_HELLO_SERIAL_LEN);
        s_peer_serial[PROTO_CLIENT_HELLO_SERIAL_LEN - 1U] = '\0';
        s_peer_valid = TRUE;
    }

    if (proto_client_serial_broadcast(s_hello_filter) != FALSE) {
        return;
    }

    if ((s_peer_valid == FALSE) ||
        (strncmp(s_hello_filter, s_peer_serial, PROTO_CLIENT_HELLO_SERIAL_LEN) != 0)) {
        s_hello_rejected = TRUE;
        s_link_up = FALSE;
        LOG_WARN("proto_client: HELLO serial mismatch (WRONG DEV)");
    }
}

static void proto_client_on_frame(uint8_t flags, uint16_t cmd, const uint8_t *payload,
                                  uint16_t len)
{
    if ((cmd == (uint16_t)(PC_CMD_HELLO | 0x8000U)) && ((flags & PC_FLAG_NAK) == 0U)) {
        proto_client_handle_hello_ack(payload, len);
    }

    if ((cmd == PC_CMD_TELEMETRY_PUSH) &&
        (((flags & PC_FLAG_UNSOLICITED) != 0U) || (len >= 5U))) {
        proto_client_handle_push(payload, len);
        return;
    }

    /* 其它 ACK：仅刷新链路 */
    (void)payload;
    (void)len;
}

static void proto_client_rx_reset(void)
{
    s_rx_state = PC_RX_SOF0;
    s_rx_hdr_n = 0U;
    s_rx_pay_n = 0U;
    s_rx_pay_len = 0U;
    s_rx_crc_n = 0U;
}

static void proto_client_rx_byte(uint8_t b)
{
    uint16_t crc_calc;
    uint16_t crc_rx;
    uint16_t cmd;
    uint8_t flags;

    s_rx_byte_count++;

    switch (s_rx_state) {
    case PC_RX_SOF0:
        if (b == PC_SOF0) {
            s_rx_state = PC_RX_SOF1;
        }
        break;
    case PC_RX_SOF1:
        if (b == PC_SOF1) {
            s_rx_state = PC_RX_HDR;
            s_rx_hdr_n = 0U;
        } else if (b != PC_SOF0) {
            s_rx_state = PC_RX_SOF0;
        }
        break;
    case PC_RX_HDR:
        s_rx_hdr[s_rx_hdr_n++] = b;
        if (s_rx_hdr_n >= PC_HEADER_SIZE) {
            if (s_rx_hdr[0] != PC_VER) {
                proto_client_rx_reset();
                break;
            }
            s_rx_pay_len = proto_client_get_u16(&s_rx_hdr[2]);
            if (s_rx_pay_len > PC_MAX_PAYLOAD) {
                proto_client_rx_reset();
                break;
            }
            s_rx_pay_n = 0U;
            s_rx_crc_n = 0U;
            if (s_rx_pay_len == 0U) {
                s_rx_state = PC_RX_CRC;
            } else {
                s_rx_state = PC_RX_PAYLOAD;
            }
        }
        break;
    case PC_RX_PAYLOAD:
        s_rx_payload[s_rx_pay_n++] = b;
        if (s_rx_pay_n >= s_rx_pay_len) {
            s_rx_state = PC_RX_CRC;
            s_rx_crc_n = 0U;
        }
        break;
    case PC_RX_CRC:
        s_rx_crc_buf[s_rx_crc_n++] = b;
        if (s_rx_crc_n >= 2U) {
            (void)memcpy(s_crc_body, s_rx_hdr, PC_HEADER_SIZE);
            if (s_rx_pay_len > 0U) {
                (void)memcpy(&s_crc_body[PC_HEADER_SIZE], s_rx_payload, s_rx_pay_len);
            }
            crc_calc = proto_client_crc16(s_crc_body, (uint16_t)(PC_HEADER_SIZE + s_rx_pay_len));
            crc_rx = proto_client_get_u16(s_rx_crc_buf);
            if (crc_calc == crc_rx) {
                flags = s_rx_hdr[1];
                cmd = proto_client_get_u16(&s_rx_hdr[4]);
                if ((flags & PC_FLAG_DIR_DEVICE) != 0U) {
                    s_frame_ok++;
                    proto_client_note_rx();
                    proto_client_on_frame(flags, cmd, s_rx_payload, s_rx_pay_len);
                }
            } else {
                s_frame_crc_fail++;
            }
            proto_client_rx_reset();
        }
        break;
    default:
        proto_client_rx_reset();
        break;
    }
}

static void proto_client_poll_rx(void)
{
    proto_client_rx_pump();
    while (s_rx_ring_tail != s_rx_ring_head) {
        uint8_t b = s_rx_ring[s_rx_ring_tail];
        s_rx_ring_tail = (uint16_t)((s_rx_ring_tail + 1U) % PC_RX_RING_SIZE);
        proto_client_rx_byte(b);
    }
}

static bool_t proto_client_fresh(bool_t have, uint32_t stamp_ms)
{
    uint32_t now;

    if (have == FALSE) {
        return FALSE;
    }
    now = proto_client_uptime_ms();
    if ((now - stamp_ms) > PROTO_CLIENT_TELEM_STALE_MS) {
        return FALSE;
    }
    return TRUE;
}

status_t proto_client_init(void)
{
    s_seq = 0U;
    s_link_up = FALSE;
    s_last_rx_ms = 0U;
    s_last_ping_ms = 0U;
    s_last_hello_ms = 0U;
    s_last_drive_ms = 0U;
    s_last_link_log_ms = 0U;
    s_rx_byte_count = 0U;
    s_last_sent_throttle = 0;
    s_last_sent_steer = 0;
    s_drive_session = FALSE;
    s_hello_filter[0] = '\0';
    s_peer_serial[0] = '\0';
    s_peer_valid = FALSE;
    s_hello_rejected = FALSE;
    s_rx_ring_head = 0U;
    s_rx_ring_tail = 0U;
    s_rx_drop_count = 0U;
    s_frame_ok = 0U;
    s_frame_crc_fail = 0U;
    s_push_bat = 0U;
    s_push_att = 0U;
    s_push_enc = 0U;
    s_push_us = 0U;
    s_push_rpm = 0U;
    s_push_other = 0U;
    s_tx_frames = 0U;
    s_tx_drive = 0U;
    s_tx_bytes = 0U;
    (void)memset(&s_stats_last_log, 0, sizeof(s_stats_last_log));
    proto_client_rx_reset();
    proto_client_telem_clear();
    return STATUS_OK;
}

void proto_client_telem_clear(void)
{
    s_bat_have = FALSE;
    s_us_have = FALSE;
    s_att_have = FALSE;
    s_rpm_have = FALSE;
    s_enc_have = FALSE;
    s_bat_pct = 0U;
    s_bat_mv = 0U;
    s_us_mm = 0U;
    s_roll = 0;
    s_pitch = 0;
    s_yaw = 0;
    (void)memset(s_rpm, 0, sizeof(s_rpm));
    (void)memset(s_enc, 0, sizeof(s_enc));
}

void proto_client_telem_get(proto_client_telem_t *out)
{
    if (out == NULL) {
        return;
    }
    (void)memset(out, 0, sizeof(*out));

    out->bat_valid = proto_client_fresh(s_bat_have, s_bat_ms);
    out->bat_pct = s_bat_pct;
    out->bat_mv = s_bat_mv;

    out->us_valid = proto_client_fresh(s_us_have, s_us_ms);
    out->us_mm = s_us_mm;

    out->att_valid = proto_client_fresh(s_att_have, s_att_ms);
    out->roll = s_roll;
    out->pitch = s_pitch;
    out->yaw = s_yaw;

    out->rpm_valid = proto_client_fresh(s_rpm_have, s_rpm_ms);
    (void)memcpy(out->rpm, s_rpm, sizeof(out->rpm));

    out->enc_valid = proto_client_fresh(s_enc_have, s_enc_ms);
    (void)memcpy(out->enc, s_enc, sizeof(out->enc));
}

void proto_client_stats_get(proto_client_stats_t *out)
{
    if (out == NULL) {
        return;
    }
    out->rx_bytes = s_rx_byte_count;
    out->ring_drop = s_rx_drop_count;
    out->frame_ok = s_frame_ok;
    out->frame_crc_fail = s_frame_crc_fail;
    out->push_bat = s_push_bat;
    out->push_att = s_push_att;
    out->push_enc = s_push_enc;
    out->push_us = s_push_us;
    out->push_rpm = s_push_rpm;
    out->push_other = s_push_other;
    out->tx_frames = s_tx_frames;
    out->tx_drive = s_tx_drive;
    out->tx_bytes = s_tx_bytes;
}

void proto_client_stats_log_delta(const char *mode_tag, uint32_t heap_free, uint32_t stk_words)
{
    proto_client_stats_t cur;
    proto_client_stats_t d;
    proto_client_telem_t telem;
    const char *tag = (mode_tag != NULL) ? mode_tag : "-";

    proto_client_stats_get(&cur);
    d.rx_bytes = cur.rx_bytes - s_stats_last_log.rx_bytes;
    d.ring_drop = cur.ring_drop - s_stats_last_log.ring_drop;
    d.frame_ok = cur.frame_ok - s_stats_last_log.frame_ok;
    d.frame_crc_fail = cur.frame_crc_fail - s_stats_last_log.frame_crc_fail;
    d.push_bat = cur.push_bat - s_stats_last_log.push_bat;
    d.push_att = cur.push_att - s_stats_last_log.push_att;
    d.push_enc = cur.push_enc - s_stats_last_log.push_enc;
    d.push_us = cur.push_us - s_stats_last_log.push_us;
    d.push_rpm = cur.push_rpm - s_stats_last_log.push_rpm;
    d.push_other = cur.push_other - s_stats_last_log.push_other;
    d.tx_frames = cur.tx_frames - s_stats_last_log.tx_frames;
    d.tx_drive = cur.tx_drive - s_stats_last_log.tx_drive;
    d.tx_bytes = cur.tx_bytes - s_stats_last_log.tx_bytes;
    s_stats_last_log = cur;

    proto_client_telem_get(&telem);

    LOG_INFO("rc:stats mode=%s link=%d rx=%luB ok=%lu crc=%lu drop=%lu(+=%lu) "
             "push bat=%lu att=%lu enc=%lu us=%lu rpm=%lu oth=%lu "
             "tx=%lu drv=%lu txB=%lu valid bat=%d att=%d enc=%d us=%d rpm=%d "
             "heap=%lu stk=%lu",
             tag,
             proto_client_link_up() ? 1 : 0,
             (unsigned long)d.rx_bytes,
             (unsigned long)d.frame_ok,
             (unsigned long)d.frame_crc_fail,
             (unsigned long)cur.ring_drop,
             (unsigned long)d.ring_drop,
             (unsigned long)d.push_bat,
             (unsigned long)d.push_att,
             (unsigned long)d.push_enc,
             (unsigned long)d.push_us,
             (unsigned long)d.push_rpm,
             (unsigned long)d.push_other,
             (unsigned long)d.tx_frames,
             (unsigned long)d.tx_drive,
             (unsigned long)d.tx_bytes,
             telem.bat_valid ? 1 : 0,
             telem.att_valid ? 1 : 0,
             telem.enc_valid ? 1 : 0,
             telem.us_valid ? 1 : 0,
             telem.rpm_valid ? 1 : 0,
             (unsigned long)heap_free,
             (unsigned long)stk_words);
}

status_t proto_client_send_hello(void)
{
    return proto_client_send_hello_to(NULL);
}

status_t proto_client_send_hello_to(const char *target_serial)
{
    uint8_t payload[PROTO_CLIENT_HELLO_SERIAL_LEN];

    (void)memset(s_hello_filter, 0, sizeof(s_hello_filter));
    (void)memset(payload, 0, sizeof(payload));
    s_hello_rejected = FALSE;
    s_peer_valid = FALSE;
    s_peer_serial[0] = '\0';

    if (proto_client_serial_broadcast(target_serial) == FALSE) {
        (void)memcpy(s_hello_filter, target_serial, PROTO_CLIENT_HELLO_SERIAL_LEN);
        (void)memcpy(payload, target_serial, PROTO_CLIENT_HELLO_SERIAL_LEN);
    }

    if (!proto_client_send(PC_CMD_HELLO, payload, (uint16_t)sizeof(payload))) {
        return STATUS_FAIL;
    }
    return STATUS_OK;
}

status_t proto_client_send_ping(void)
{
    if (!proto_client_send(PC_CMD_PING, NULL, 0U)) {
        return STATUS_FAIL;
    }
    return STATUS_OK;
}

status_t proto_client_subscribe(uint32_t optional_mask)
{
    uint8_t payload[14];
    uint32_t mask;

    mask = optional_mask | PROTO_CLIENT_CH_ATTITUDE | PROTO_CLIENT_CH_ENCODER;
    proto_client_put_u32(&payload[0], mask);
    payload[4] = PC_HZ_ATT;
    payload[5] = PC_HZ_ENC;
    payload[6] = 0U;
    payload[7] = ((optional_mask & PROTO_CLIENT_CH_ULTRASONIC) != 0U) ? PC_HZ_ULTRA : 0U;
    payload[8] = ((optional_mask & PROTO_CLIENT_CH_MOTOR_RPM) != 0U) ? PC_HZ_RPM : 0U;
    payload[9] = 0U;
    payload[10] = 0U;
    payload[11] = 0U;
    payload[12] = 0U;
    payload[13] = 0U;

    if (!proto_client_send(PC_CMD_SUBSCRIBE, payload, (uint16_t)sizeof(payload))) {
        return STATUS_FAIL;
    }
    LOG_INFO("proto_client: SUBSCRIBE mask=0x%08lx", (unsigned long)mask);
    return STATUS_OK;
}

status_t proto_client_unsubscribe_optional(void)
{
    uint8_t payload[4];

    proto_client_put_u32(&payload[0], 0xFFFFFFFFU);
    if (!proto_client_send(PC_CMD_UNSUBSCRIBE, payload, (uint16_t)sizeof(payload))) {
        return STATUS_FAIL;
    }
    LOG_INFO("proto_client: UNSUBSCRIBE optional");
    return STATUS_OK;
}

status_t proto_client_send_drive(int16_t throttle, int16_t steer)
{
    uint8_t payload[4];

    if (throttle > PROTO_CLIENT_DRIVE_THROTTLE_MAX) {
        throttle = PROTO_CLIENT_DRIVE_THROTTLE_MAX;
    } else if (throttle < -PROTO_CLIENT_DRIVE_THROTTLE_MAX) {
        throttle = (int16_t)(-PROTO_CLIENT_DRIVE_THROTTLE_MAX);
    }
    if (steer > PROTO_CLIENT_DRIVE_STEER_MAX) {
        steer = PROTO_CLIENT_DRIVE_STEER_MAX;
    } else if (steer < -PROTO_CLIENT_DRIVE_STEER_MAX) {
        steer = (int16_t)(-PROTO_CLIENT_DRIVE_STEER_MAX);
    }

    payload[0] = (uint8_t)((uint16_t)throttle & 0xFFU);
    payload[1] = (uint8_t)(((uint16_t)throttle >> 8) & 0xFFU);
    payload[2] = (uint8_t)((uint16_t)steer & 0xFFU);
    payload[3] = (uint8_t)(((uint16_t)steer >> 8) & 0xFFU);

    if (!proto_client_send(PC_CMD_DRIVE, payload, (uint16_t)sizeof(payload))) {
        return STATUS_FAIL;
    }
    s_last_drive_ms = proto_client_uptime_ms();
    return STATUS_OK;
}

status_t proto_client_send_drive_stop(void)
{
    if (!proto_client_send(PC_CMD_DRIVE_STOP, NULL, 0U)) {
        return STATUS_FAIL;
    }
    s_drive_session = FALSE;
    s_last_sent_throttle = 0;
    s_last_sent_steer = 0;
    s_last_drive_ms = 0U;
    return STATUS_OK;
}

void proto_client_drive_update(int16_t throttle, int16_t steer, bool_t muted)
{
    uint32_t now;
    uint32_t min_period;
    bool_t idle;
    bool_t changed;

    if (s_link_up == FALSE) {
        return;
    }

    if (muted != FALSE) {
        throttle = 0;
        steer = 0;
    }

    if (throttle > PROTO_CLIENT_DRIVE_THROTTLE_MAX) {
        throttle = PROTO_CLIENT_DRIVE_THROTTLE_MAX;
    } else if (throttle < -PROTO_CLIENT_DRIVE_THROTTLE_MAX) {
        throttle = (int16_t)(-PROTO_CLIENT_DRIVE_THROTTLE_MAX);
    }
    if (steer > PROTO_CLIENT_DRIVE_STEER_MAX) {
        steer = PROTO_CLIENT_DRIVE_STEER_MAX;
    } else if (steer < -PROTO_CLIENT_DRIVE_STEER_MAX) {
        steer = (int16_t)(-PROTO_CLIENT_DRIVE_STEER_MAX);
    }

    idle = ((throttle == 0) && (steer == 0)) ? TRUE : FALSE;
    if (idle != FALSE) {
        if (s_drive_session != FALSE) {
            (void)proto_client_send_drive_stop();
        }
        return;
    }

    now = proto_client_uptime_ms();
    changed = ((throttle != s_last_sent_throttle) || (steer != s_last_sent_steer)) ? TRUE : FALSE;
    min_period = (changed != FALSE) ? PC_DRIVE_MIN_PERIOD_MS : PC_DRIVE_HOLD_PERIOD_MS;
    if ((s_last_drive_ms != 0U) && ((now - s_last_drive_ms) < min_period)) {
        return;
    }

    if (proto_client_send_drive(throttle, steer) == STATUS_OK) {
        s_last_sent_throttle = throttle;
        s_last_sent_steer = steer;
        s_drive_session = TRUE;
    }
}

bool_t proto_client_link_up(void)
{
    return s_link_up;
}

bool_t proto_client_hello_rejected(void)
{
    return s_hello_rejected;
}

void proto_client_peer_serial(char *buf, size_t buflen)
{
    if ((buf == NULL) || (buflen == 0U)) {
        return;
    }
    buf[0] = '\0';
    if (s_peer_valid == FALSE) {
        return;
    }
    (void)snprintf(buf, buflen, "%s", s_peer_serial);
}

void proto_client_set_auto_hello(bool_t enable)
{
    s_auto_hello = enable;
    if (enable == FALSE) {
        s_last_hello_ms = 0U;
    }
}

bool_t proto_client_auto_hello(void)
{
    return s_auto_hello;
}

void proto_client_tick(uint32_t period_ms)
{
    uint32_t now = proto_client_uptime_ms();

    (void)period_ms;
    proto_client_poll_rx();

    if (s_link_up == FALSE) {
        if (s_auto_hello != FALSE) {
            if ((s_last_hello_ms == 0U) || ((now - s_last_hello_ms) >= PC_HELLO_RETRY_MS)) {
                if (proto_client_send_hello() == STATUS_OK) {
                    LOG_INFO("proto_client: HELLO (retry, waiting RX)");
                }
                s_last_hello_ms = (now == 0U) ? 1U : now;
            }
            if ((now - s_last_link_log_ms) >= PC_LINK_LOG_PERIOD_MS) {
                s_last_link_log_ms = now;
                LOG_INFO("proto_client: waiting BT link (UART0, no RX yet)");
            }
        }
    } else if ((now - s_last_ping_ms) >= PC_PING_PERIOD_MS) {
        (void)proto_client_send_ping();
        s_last_ping_ms = now;
    }

    if (s_link_up && (s_last_rx_ms != 0U) && ((now - s_last_rx_ms) > PC_LINK_TIMEOUT_MS)) {
        s_link_up = FALSE;
        s_drive_session = FALSE;
        s_last_hello_ms = 0U;
        proto_client_telem_clear();
        LOG_WARN("proto_client: link timeout (no RX %lums), drop=%lu, retry HELLO",
                 (unsigned long)PC_LINK_TIMEOUT_MS, (unsigned long)s_rx_drop_count);
    }

    if ((s_drive_session != FALSE) && (s_last_drive_ms != 0U) &&
        ((now - s_last_drive_ms) > PC_DRIVE_IDLE_STOP_MS)) {
        (void)proto_client_send_drive_stop();
    }
}
