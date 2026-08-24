/**
 * @file    proto_client.c
 * @brief   遥控器协议主机：经 BOARD_UART_BT_CFG 向小车发送 HELLO / DRIVE
 */

#include "proto_client.h"

#include "board.h"
#include "log.h"

#include "bsp_uart.h"
#include "bsp_systick.h"

#include "driverlib/uart.h"

#include <string.h>

#define PC_SOF0                 0x54U
#define PC_SOF1                 0x4DU
#define PC_VER                  0x02U
#define PC_HEADER_SIZE          7U
#define PC_MAX_PAYLOAD          256U
#define PC_MAX_FRAME            (2U + PC_HEADER_SIZE + PC_MAX_PAYLOAD + 2U)

#define PC_CMD_HELLO            0x0001U
#define PC_CMD_PING             0x0002U
#define PC_CMD_DRIVE            0x0030U
#define PC_CMD_DRIVE_STOP       0x0031U

#define PC_PING_PERIOD_MS       1000U
#define PC_HELLO_RETRY_MS       1000U
#define PC_LINK_TIMEOUT_MS      8000U
#define PC_DRIVE_IDLE_STOP_MS   500U
#define PC_DRIVE_MIN_PERIOD_MS  50U
#define PC_LINK_LOG_PERIOD_MS   5000U

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

static uint32_t proto_client_uptime_ms(void)
{
    return bsp_get_tick_ms();
}

static void proto_client_put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFFU);
    p[1] = (uint8_t)((v >> 8) & 0xFFU);
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

    for (i = 0U; i < total; i++) {
        UARTCharPut(base, frame[i]);
    }
    while (UARTBusy(base)) {
    }
    return TRUE;
}

static void proto_client_poll_rx(void)
{
    uint32_t base = BOARD_UART_BT_CFG.base;
    int32_t ch;
    bool_t got = FALSE;

    while (UARTCharsAvail(base)) {
        ch = UARTCharGetNonBlocking(base);
        if (ch < 0) {
            break;
        }
        s_rx_byte_count++;
        s_last_rx_ms = proto_client_uptime_ms();
        got = TRUE;
    }

    if (got != FALSE) {
        if (s_link_up == FALSE) {
            s_link_up = TRUE;
            LOG_INFO("proto_client: link UP (rx=%lu B)",
                     (unsigned long)s_rx_byte_count);
        } else {
            s_link_up = TRUE;
        }
    }
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
    return STATUS_OK;
}

status_t proto_client_send_hello(void)
{
    if (!proto_client_send(PC_CMD_HELLO, NULL, 0U)) {
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
    /* 变化：最快 25Hz；不变：约 5Hz 保活（小车 500ms 无帧会停车） */
    min_period = (changed != FALSE) ? PC_DRIVE_MIN_PERIOD_MS : 200U;
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

void proto_client_tick(uint32_t period_ms)
{
    uint32_t now = proto_client_uptime_ms();

    (void)period_ms;
    proto_client_poll_rx();

    /* 未建链：周期重发 HELLO（上电时对端可能尚未配对/未就绪） */
    if (s_link_up == FALSE) {
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
    } else if ((now - s_last_ping_ms) >= PC_PING_PERIOD_MS) {
        (void)proto_client_send_ping();
        s_last_ping_ms = now;
    }

    if (s_link_up && (s_last_rx_ms != 0U) && ((now - s_last_rx_ms) > PC_LINK_TIMEOUT_MS)) {
        s_link_up = FALSE;
        s_drive_session = FALSE;
        s_last_hello_ms = 0U;
        LOG_WARN("proto_client: link timeout (no RX %lums), retry HELLO",
                 (unsigned long)PC_LINK_TIMEOUT_MS);
    }

    /* drive_update 负责会话停驶；此处仅兜底：曾发 DRIVE 后长时间未再 update */
    if ((s_drive_session != FALSE) && (s_last_drive_ms != 0U) &&
        ((now - s_last_drive_ms) > PC_DRIVE_IDLE_STOP_MS)) {
        (void)proto_client_send_drive_stop();
    }
}
