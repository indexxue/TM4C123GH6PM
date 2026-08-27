/**
 * @file    rc_link.c
 * @brief   手动建链状态机；上电不自动 HELLO
 */

#include "rc_link.h"

#include "log.h"
#include "proto_client.h"
#include "rc_target.h"

#define RC_LINK_HELLO_BURST_MS    200U
#define RC_LINK_CONNECT_TIMEOUT_MS 5000U

static rc_link_state_t s_state = RC_LINK_OFF;
static uint16_t s_connect_ms;
static uint16_t s_hello_burst_ms;
static bool_t s_wrong_device;

static status_t rc_link_send_hello_filtered(void)
{
    return proto_client_send_hello_to(rc_target_active_serial());
}

static void rc_link_sync_state(void)
{
    if (proto_client_link_up() != FALSE) {
        if (s_state == RC_LINK_CONNECTING) {
            (void)rc_target_bind_peer_serial(0U);
            LOG_INFO("rc_link: connected");
        }
        s_state = RC_LINK_CONNECTED;
        s_connect_ms = 0U;
        s_hello_burst_ms = 0U;
        s_wrong_device = FALSE;
        return;
    }

    if (s_state == RC_LINK_CONNECTED) {
        LOG_WARN("rc_link: link down");
        s_state = RC_LINK_OFF;
    }
}

status_t rc_link_init(void)
{
    (void)proto_client_init();
    proto_client_set_auto_hello(FALSE);
    s_state = RC_LINK_OFF;
    s_connect_ms = 0U;
    s_hello_burst_ms = 0U;
    s_wrong_device = FALSE;
    LOG_INFO("rc_link: manual connect (JS1 to HELLO)");
    return STATUS_OK;
}

void rc_link_tick(uint32_t dt_ms)
{
    proto_client_tick(dt_ms);
    rc_link_sync_state();

    if (s_state != RC_LINK_CONNECTING) {
        return;
    }

    if (proto_client_hello_rejected() != FALSE) {
        LOG_WARN("rc_link: wrong device (serial filter)");
        s_wrong_device = TRUE;
        s_state = RC_LINK_OFF;
        s_connect_ms = 0U;
        s_hello_burst_ms = 0U;
        return;
    }

    s_connect_ms = (uint16_t)(s_connect_ms + dt_ms);
    s_hello_burst_ms = (uint16_t)(s_hello_burst_ms + dt_ms);

    if (s_hello_burst_ms >= RC_LINK_HELLO_BURST_MS) {
        s_hello_burst_ms = 0U;
        if (rc_link_send_hello_filtered() == STATUS_OK) {
            LOG_INFO("rc_link: HELLO (connecting)");
        }
    }

    if (s_connect_ms >= RC_LINK_CONNECT_TIMEOUT_MS) {
        LOG_WARN("rc_link: connect timeout");
        s_state = RC_LINK_OFF;
        s_connect_ms = 0U;
        s_hello_burst_ms = 0U;
    }
}

rc_link_state_t rc_link_state(void)
{
    return s_state;
}

bool_t rc_link_up(void)
{
    return proto_client_link_up();
}

bool_t rc_link_wrong_device(void)
{
    return s_wrong_device;
}

status_t rc_link_connect(void)
{
    if (s_state == RC_LINK_CONNECTED) {
        return STATUS_OK;
    }
    if (s_state == RC_LINK_CONNECTING) {
        return STATUS_OK;
    }

    s_wrong_device = FALSE;
    s_state = RC_LINK_CONNECTING;
    s_connect_ms = 0U;
    s_hello_burst_ms = 0U;
    if (rc_link_send_hello_filtered() == STATUS_OK) {
        LOG_INFO("rc_link: connect start target=%s",
                 rc_target_active_serial()[0] != '\0' ? rc_target_active_serial() : "any");
    }
    return STATUS_OK;
}

void rc_link_cancel_connect(void)
{
    if (s_state != RC_LINK_CONNECTING) {
        return;
    }
    s_state = RC_LINK_OFF;
    s_connect_ms = 0U;
    s_hello_burst_ms = 0U;
    s_wrong_device = FALSE;
    LOG_INFO("rc_link: connect cancelled");
}

status_t rc_link_send_drive_stop(void)
{
    return proto_client_send_drive_stop();
}

status_t rc_link_subscribe(uint32_t optional_mask)
{
    return proto_client_subscribe(optional_mask);
}

status_t rc_link_unsubscribe_optional(void)
{
    return proto_client_unsubscribe_optional();
}

void rc_link_drive_update(int16_t throttle, int16_t steer, bool_t muted)
{
    proto_client_drive_update(throttle, steer, muted);
}

void rc_link_telem_clear(void)
{
    proto_client_telem_clear();
}

void rc_link_telem_get(proto_client_telem_t *out)
{
    proto_client_telem_get(out);
}
