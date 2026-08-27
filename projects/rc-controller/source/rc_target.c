/**
 * @file    rc_target.c
 * @brief   ns=rc key=targets + target_idx
 */

#include "rc_target.h"

#include "log.h"
#include "nvs.h"
#include "proto_client.h"

#include "crc32.h"

#include <string.h>

#define RC_TARGET_NVS_NS        "rc"
#define RC_TARGET_NVS_KEY       "targets"
#define RC_TARGET_MAGIC         0x5447U /* 'T''G' */
#define RC_TARGET_VERSION       1U

typedef struct {
    char nickname[10];
    char serial[16];
    uint32_t last_caps;
} rc_target_entry_t;

typedef struct {
    uint16_t magic;
    uint16_t version;
    uint8_t count;
    uint8_t active_idx;
    rc_target_entry_t targets[RC_TARGET_MAX];
    uint32_t crc32;
} rc_targets_blob_t;

static rc_target_t s_targets[RC_TARGET_MAX];
static uint8_t s_count;
static uint8_t s_active;

static void rc_target_fill_crc(rc_targets_blob_t *b)
{
    rc_targets_blob_t tmp;

    if (b == NULL) {
        return;
    }
    tmp = *b;
    tmp.crc32 = 0U;
    b->crc32 = crc32_compute(&tmp, sizeof(tmp));
}

static bool_t rc_target_validate_blob(const rc_targets_blob_t *b)
{
    rc_targets_blob_t tmp;
    uint32_t crc;

    if (b == NULL) {
        return FALSE;
    }
    if ((b->magic != RC_TARGET_MAGIC) || (b->version != RC_TARGET_VERSION)) {
        return FALSE;
    }
    if ((b->count == 0U) || (b->count > RC_TARGET_MAX)) {
        return FALSE;
    }
    if (b->active_idx >= b->count) {
        return FALSE;
    }
    tmp = *b;
    tmp.crc32 = 0U;
    crc = crc32_compute(&tmp, sizeof(tmp));
    return (crc == b->crc32) ? TRUE : FALSE;
}

static void rc_target_entry_to_public(const rc_target_entry_t *e, rc_target_t *out)
{
    if ((e == NULL) || (out == NULL)) {
        return;
    }
    (void)memset(out, 0, sizeof(*out));
    (void)memcpy(out->nickname, e->nickname, sizeof(e->nickname));
    out->nickname[RC_TARGET_NICK_LEN - 1U] = '\0';
    (void)memcpy(out->serial, e->serial, sizeof(e->serial));
    out->serial[RC_TARGET_SERIAL_LEN - 1U] = '\0';
    out->last_caps = e->last_caps;
}

static void rc_target_public_to_entry(const rc_target_t *in, rc_target_entry_t *e)
{
    if ((in == NULL) || (e == NULL)) {
        return;
    }
    (void)memset(e, 0, sizeof(*e));
    (void)memcpy(e->nickname, in->nickname, sizeof(e->nickname));
    (void)memcpy(e->serial, in->serial, sizeof(e->serial));
    e->last_caps = in->last_caps;
}

static void rc_target_load_defaults(void)
{
    static const rc_target_t defs[RC_TARGET_MAX] = {
        { .nickname = "Any", .serial = "", .last_caps = 0U },
        { .nickname = "Car-A", .serial = "", .last_caps = 0U },
        { .nickname = "Car-B", .serial = "", .last_caps = 0U },
        { .nickname = "Car-C", .serial = "", .last_caps = 0U },
    };
    uint8_t i;

    s_count = RC_TARGET_MAX;
    s_active = 0U;
    for (i = 0U; i < RC_TARGET_MAX; i++) {
        s_targets[i] = defs[i];
    }
}

static status_t rc_target_save(void)
{
    rc_targets_blob_t blob;
    uint8_t i;

    (void)memset(&blob, 0, sizeof(blob));
    blob.magic = RC_TARGET_MAGIC;
    blob.version = RC_TARGET_VERSION;
    blob.count = s_count;
    blob.active_idx = s_active;
    for (i = 0U; i < s_count; i++) {
        rc_target_public_to_entry(&s_targets[i], &blob.targets[i]);
    }
    rc_target_fill_crc(&blob);

    if (nvs_set_blob(RC_TARGET_NVS_NS, RC_TARGET_NVS_KEY, &blob, (uint32_t)sizeof(blob)) !=
        STATUS_OK) {
        return STATUS_FAIL;
    }
    return STATUS_OK;
}

status_t rc_target_init(void)
{
    rc_targets_blob_t blob;
    uint32_t len = (uint32_t)sizeof(blob);

    rc_target_load_defaults();
    if (nvs_get_blob(RC_TARGET_NVS_NS, RC_TARGET_NVS_KEY, &blob, &len) == STATUS_OK) {
        if ((len == (uint32_t)sizeof(blob)) && (rc_target_validate_blob(&blob) != FALSE)) {
            uint8_t i;

            s_count = blob.count;
            s_active = blob.active_idx;
            for (i = 0U; i < s_count; i++) {
                rc_target_entry_to_public(&blob.targets[i], &s_targets[i]);
            }
            LOG_INFO("rc_target: loaded count=%u active=%u nick=%s",
                     (unsigned)s_count, (unsigned)s_active, s_targets[s_active].nickname);
            return STATUS_OK;
        }
    }

    (void)rc_target_save();
    LOG_INFO("rc_target: defaults active=%s", s_targets[s_active].nickname);
    return STATUS_OK;
}

uint8_t rc_target_count(void)
{
    return s_count;
}

uint8_t rc_target_active_index(void)
{
    return s_active;
}

const rc_target_t *rc_target_get(uint8_t idx)
{
    if (idx >= s_count) {
        return NULL;
    }
    return &s_targets[idx];
}

const rc_target_t *rc_target_active(void)
{
    return rc_target_get(s_active);
}

const char *rc_target_active_serial(void)
{
    const rc_target_t *t = rc_target_active();

    if (t == NULL) {
        return "";
    }
    return t->serial;
}

status_t rc_target_set_active(uint8_t idx)
{
    if (idx >= s_count) {
        return STATUS_FAIL;
    }
    s_active = idx;
    if (rc_target_save() != STATUS_OK) {
        return STATUS_FAIL;
    }
    LOG_INFO("rc_target: active idx=%u nick=%s", (unsigned)idx, s_targets[idx].nickname);
    return STATUS_OK;
}

status_t rc_target_bind_peer_serial(uint32_t caps)
{
    char peer[PROTO_CLIENT_HELLO_SERIAL_LEN];
    rc_target_t *t;

    proto_client_peer_serial(peer, sizeof(peer));
    if (peer[0] == '\0') {
        return STATUS_OK;
    }

    t = &s_targets[s_active];
    if (t->serial[0] != '\0') {
        t->last_caps = caps;
        return rc_target_save();
    }

    (void)memcpy(t->serial, peer, PROTO_CLIENT_HELLO_SERIAL_LEN);
    t->serial[RC_TARGET_SERIAL_LEN - 1U] = '\0';
    t->last_caps = caps;
    LOG_INFO("rc_target: bind slot %u serial=%s", (unsigned)s_active, peer);
    return rc_target_save();
}
