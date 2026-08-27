/**
 * @file    rc_model.c
 * @brief   ns=rc key=models blob（≤128 B）
 */

#include "rc_model.h"

#include "log.h"
#include "nvs.h"
#include "rc_sub.h"

#include "crc32.h"

#include <string.h>

#define RC_MODEL_NVS_NS        "rc"
#define RC_MODEL_NVS_KEY       "models"
#define RC_MODEL_MAGIC         0x4D52U /* 'M''R' */
#define RC_MODEL_VERSION       1U
#define RC_MODEL_SUB_MASK      \
    (PROTO_CLIENT_CH_BATTERY | PROTO_CLIENT_CH_ULTRASONIC | PROTO_CLIENT_CH_MOTOR_RPM)

typedef struct {
    char name[6];
    uint8_t input_src;
    uint8_t target_slot;
    uint32_t sub_mask;
} rc_model_entry_t;

typedef struct {
    uint16_t magic;
    uint16_t version;
    uint8_t count;
    uint8_t active_idx;
    rc_model_entry_t models[RC_MODEL_MAX];
    uint32_t crc32;
} rc_models_blob_t;

static rc_model_t s_models[RC_MODEL_MAX];
static uint8_t s_count;
static uint8_t s_active;

static void rc_model_fill_crc(rc_models_blob_t *b)
{
    rc_models_blob_t tmp;

    if (b == NULL) {
        return;
    }
    tmp = *b;
    tmp.crc32 = 0U;
    b->crc32 = crc32_compute(&tmp, sizeof(tmp));
}

static bool_t rc_model_validate_blob(const rc_models_blob_t *b)
{
    rc_models_blob_t tmp;
    uint32_t crc;
    uint8_t i;

    if (b == NULL) {
        return FALSE;
    }
    if ((b->magic != RC_MODEL_MAGIC) || (b->version != RC_MODEL_VERSION)) {
        return FALSE;
    }
    if ((b->count == 0U) || (b->count > RC_MODEL_MAX)) {
        return FALSE;
    }
    if (b->active_idx >= b->count) {
        return FALSE;
    }
    for (i = 0U; i < b->count; i++) {
        if ((b->models[i].input_src != RC_MODEL_INPUT_STICK) &&
            (b->models[i].input_src != RC_MODEL_INPUT_IMU_TILT)) {
            return FALSE;
        }
        if ((b->models[i].sub_mask & ~RC_MODEL_SUB_MASK) != 0U) {
            return FALSE;
        }
    }
    tmp = *b;
    tmp.crc32 = 0U;
    crc = crc32_compute(&tmp, sizeof(tmp));
    return (crc == b->crc32) ? TRUE : FALSE;
}

static void rc_model_entry_to_public(const rc_model_entry_t *e, rc_model_t *out)
{
    if ((e == NULL) || (out == NULL)) {
        return;
    }
    (void)memset(out, 0, sizeof(*out));
    (void)memcpy(out->name, e->name, sizeof(e->name));
    out->name[RC_MODEL_NAME_LEN - 1U] = '\0';
    out->input_src = (rc_model_input_src_t)e->input_src;
    out->target_slot = e->target_slot;
    out->sub_mask = e->sub_mask;
}

static void rc_model_public_to_entry(const rc_model_t *in, rc_model_entry_t *e)
{
    if ((in == NULL) || (e == NULL)) {
        return;
    }
    (void)memset(e, 0, sizeof(*e));
    (void)memcpy(e->name, in->name, sizeof(e->name));
    e->input_src = (uint8_t)in->input_src;
    e->target_slot = in->target_slot;
    e->sub_mask = in->sub_mask & RC_MODEL_SUB_MASK;
}

static void rc_model_load_defaults(void)
{
    static const rc_model_t defs[RC_MODEL_DEFAULT_COUNT] = {
        { .name = "Stick", .input_src = RC_MODEL_INPUT_STICK, .target_slot = 0U,
          .sub_mask = RC_MODEL_SUB_MASK },
        { .name = "Line", .input_src = RC_MODEL_INPUT_STICK, .target_slot = 0U,
          .sub_mask = RC_MODEL_SUB_MASK },
        { .name = "Tilt", .input_src = RC_MODEL_INPUT_IMU_TILT, .target_slot = 0U,
          .sub_mask = RC_MODEL_SUB_MASK },
    };
    uint8_t i;

    s_count = RC_MODEL_DEFAULT_COUNT;
    s_active = 0U;
    for (i = 0U; i < RC_MODEL_DEFAULT_COUNT; i++) {
        s_models[i] = defs[i];
    }
    for (i = RC_MODEL_DEFAULT_COUNT; i < RC_MODEL_MAX; i++) {
        (void)memset(&s_models[i], 0, sizeof(s_models[i]));
    }
}

static status_t rc_model_save(void)
{
    rc_models_blob_t blob;
    uint8_t i;

    (void)memset(&blob, 0, sizeof(blob));
    blob.magic = RC_MODEL_MAGIC;
    blob.version = RC_MODEL_VERSION;
    blob.count = s_count;
    blob.active_idx = s_active;
    for (i = 0U; i < s_count; i++) {
        rc_model_public_to_entry(&s_models[i], &blob.models[i]);
    }
    rc_model_fill_crc(&blob);

    if (nvs_set_blob(RC_MODEL_NVS_NS, RC_MODEL_NVS_KEY, &blob, (uint32_t)sizeof(blob)) !=
        STATUS_OK) {
        LOG_WARN("rc_model: save fail");
        return STATUS_FAIL;
    }
    return STATUS_OK;
}

static void rc_model_load_from_blob(const rc_models_blob_t *blob)
{
    uint8_t i;

    s_count = blob->count;
    s_active = blob->active_idx;
    for (i = 0U; i < s_count; i++) {
        rc_model_entry_to_public(&blob->models[i], &s_models[i]);
    }
}

status_t rc_model_init(void)
{
    rc_models_blob_t blob;
    uint32_t len = (uint32_t)sizeof(blob);

    rc_model_load_defaults();
    if (nvs_get_blob(RC_MODEL_NVS_NS, RC_MODEL_NVS_KEY, &blob, &len) == STATUS_OK) {
        if ((len == (uint32_t)sizeof(blob)) && (rc_model_validate_blob(&blob) != FALSE)) {
            rc_model_load_from_blob(&blob);
            LOG_INFO("rc_model: loaded count=%u active=%u name=%s",
                     (unsigned)s_count, (unsigned)s_active, s_models[s_active].name);
            (void)rc_model_apply_sub();
            return STATUS_OK;
        }
    }

    (void)rc_model_save();
    (void)rc_model_apply_sub();
    LOG_INFO("rc_model: defaults count=%u active=%s",
             (unsigned)s_count, s_models[s_active].name);
    return STATUS_OK;
}

uint8_t rc_model_count(void)
{
    return s_count;
}

uint8_t rc_model_active_index(void)
{
    return s_active;
}

const rc_model_t *rc_model_get(uint8_t idx)
{
    if (idx >= s_count) {
        return NULL;
    }
    return &s_models[idx];
}

const rc_model_t *rc_model_active(void)
{
    return rc_model_get(s_active);
}

status_t rc_model_set_active(uint8_t idx)
{
    if (idx >= s_count) {
        return STATUS_FAIL;
    }
    s_active = idx;
    if (rc_model_save() != STATUS_OK) {
        return STATUS_FAIL;
    }
    LOG_INFO("rc_model: active idx=%u name=%s", (unsigned)idx, s_models[idx].name);
    return rc_model_apply_sub();
}

status_t rc_model_apply_sub(void)
{
    const rc_model_t *m = rc_model_active();

    if (m == NULL) {
        return STATUS_FAIL;
    }
    return rc_sub_save_mask(m->sub_mask);
}

status_t rc_model_cycle_next(void)
{
    uint8_t next;

    if (s_count == 0U) {
        return STATUS_FAIL;
    }
    next = (uint8_t)((s_active + 1U) % s_count);
    return rc_model_set_active(next);
}
