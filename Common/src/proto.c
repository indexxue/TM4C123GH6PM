/**
 * @file    proto.c
 * @brief   UART0 蓝牙协议：帧解析、HELLO/PING、遥测、参数读写、遥控
 */

#include "proto.h"

#include "attitude.h"
#include "battery.h"
#include "board.h"
#include "cfg.h"
#include "device_profile.h"
#include "log.h"
#include "nvs.h"
#include "led_scene.h"
#include "ultrasonic.h"

#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

#include <string.h>

/* -------------------------------------------------------------------------- */
/* 协议常量                                                                   */
/* -------------------------------------------------------------------------- */

#define PROTO_SOF0                  0x54U
#define PROTO_SOF1                  0x4DU
#define PROTO_VER                   0x01U
#define PROTO_HEADER_SIZE           7U
#define PROTO_MAX_PAYLOAD           256U
#define PROTO_RESPONSE_BIT          0x8000U

#define PROTO_FLAG_DIR_DEVICE       0x01U
#define PROTO_FLAG_NAK              0x02U
#define PROTO_FLAG_UNSOLICITED      0x04U

#define PROTO_CMD_HELLO             0x0001U
#define PROTO_CMD_PING              0x0002U
#define PROTO_CMD_GET_TELEMETRY     0x0010U
#define PROTO_CMD_SUBSCRIBE         0x0011U
#define PROTO_CMD_UNSUBSCRIBE       0x0012U
#define PROTO_CMD_TELEMETRY_PUSH    0x8011U
#define PROTO_CMD_PARAM_LIST        0x0020U
#define PROTO_CMD_PARAM_READ        0x0021U
#define PROTO_CMD_PARAM_WRITE       0x0022U
#define PROTO_CMD_DRIVE             0x0030U
#define PROTO_CMD_DRIVE_STOP        0x0031U

#define PROTO_ERR_UNKNOWN_CMD       0x02U
#define PROTO_ERR_BAD_LEN           0x03U
#define PROTO_ERR_PARAM_ID_INVALID  0x04U
#define PROTO_ERR_PARAM_READ_ONLY   0x05U
#define PROTO_ERR_PARAM_VALUE_INVALID 0x06U
#define PROTO_ERR_NVS_WRITE_FAIL    0x07U
#define PROTO_ERR_BUSY              0x08U
#define PROTO_ERR_UNSUPPORTED       0x09U

#define PROTO_CAP_TELEMETRY         (1U << 0)
#define PROTO_CAP_PARAM_RW          (1U << 1)
#define PROTO_CAP_SUBSCRIBE         (1U << 2)
#define PROTO_CAP_DRIVE             (1U << 3)

#define PROTO_CH_ATTITUDE           (1U << 1)
#define PROTO_CH_ENCODER            (1U << 2)
#define PROTO_CH_LINE_ADC           (1U << 3)
#define PROTO_CH_ULTRASONIC         (1U << 4)

#define PROTO_PUSH_CH_ATTITUDE      1U
#define PROTO_PUSH_CH_ENCODER       2U
#define PROTO_PUSH_CH_LINE_ADC      3U
#define PROTO_PUSH_CH_ULTRASONIC    4U

#define PROTO_DEFAULT_HZ_ATT        50U
#define PROTO_DEFAULT_HZ_ENC          20U
#define PROTO_DEFAULT_HZ_LINE         20U
#define PROTO_DEFAULT_HZ_ULTRA      10U

/** 超声波未连接/未就绪/测距失败时的占位距离（mm） */
#define PROTO_ULTRA_INVALID_MM      9999U

#define PROTO_DRIVE_TIMEOUT_MS      500U
#define PROTO_DRIVE_THROTTLE_MAX    1000
#define PROTO_DRIVE_STEER_MAX       1000

#define PROTO_TASK_NAME_RX          "proto_rx"
#define PROTO_RX_STACK_WORDS        (768U)
#define PROTO_RX_PRIORITY           (2U)
#define PROTO_RX_POLL_MS            (20U)

#define PROTO_LINE_ADC_COUNT        LINE_SENSOR_COUNT
#define PROTO_ENCODER_COUNT         4U

/* -------------------------------------------------------------------------- */
/* 参数元数据                                                                 */
/* -------------------------------------------------------------------------- */

typedef status_t (*proto_param_write_fn)(const uint8_t *data, uint16_t len);

typedef struct {
    nvs_param_id_t id;
    uint8_t size;
    proto_param_write_fn write_fn;
} proto_param_desc_t;

/* -------------------------------------------------------------------------- */
/* 模块状态                                                                   */
/* -------------------------------------------------------------------------- */

static TaskHandle_t s_rx_task;
static SemaphoreHandle_t s_tx_mutex;
static volatile bool_t s_echo_mode;

static struct {
    uint32_t mask;
    uint8_t hz_att;
    uint8_t hz_enc;
    uint8_t hz_line;
    uint8_t hz_ultra;
    uint32_t acc_att_ms;
    uint32_t acc_enc_ms;
    uint32_t acc_line_ms;
    uint32_t acc_ultra_ms;
} s_sub;

static bool_t s_ultra_ready;
static bool_t s_ultra_init_attempted;

static volatile bool s_drive_active;
static volatile bool s_drive_stop_req;
static volatile int16_t s_drive_throttle;
static volatile int16_t s_drive_steer;
static volatile uint32_t s_drive_last_ms;

typedef enum {
    PROTO_PARSE_SOF0 = 0,
    PROTO_PARSE_SOF1,
    PROTO_PARSE_BODY,
} proto_parse_state_t;

static struct {
    proto_parse_state_t state;
    uint8_t body[PROTO_HEADER_SIZE + PROTO_MAX_PAYLOAD + 2U];
    uint16_t body_len;
} s_parser;

static uint8_t s_tx_frame[2U + PROTO_HEADER_SIZE + PROTO_MAX_PAYLOAD + 2U];

/* -------------------------------------------------------------------------- */
/* CRC16-CCITT-FALSE                                                          */
/* -------------------------------------------------------------------------- */

static uint16_t proto_crc16_ccitt_false(const uint8_t *data, uint16_t len)
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

/* -------------------------------------------------------------------------- */
/* 字节序辅助                                                                 */
/* -------------------------------------------------------------------------- */

static uint32_t proto_uptime_ms(void)
{
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

static void proto_put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFFU);
    p[1] = (uint8_t)((v >> 8) & 0xFFU);
}

static void proto_put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFU);
    p[1] = (uint8_t)((v >> 8) & 0xFFU);
    p[2] = (uint8_t)((v >> 16) & 0xFFU);
    p[3] = (uint8_t)((v >> 24) & 0xFFU);
}

static void proto_put_f32(uint8_t *p, float v)
{
    uint32_t bits;

    (void)memcpy(&bits, &v, sizeof(bits));
    proto_put_u32(p, bits);
}

static uint16_t proto_get_u16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t proto_get_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static int16_t proto_get_i16(const uint8_t *p)
{
    return (int16_t)proto_get_u16(p);
}

/* -------------------------------------------------------------------------- */
/* UART0 发送                                                                 */
/* -------------------------------------------------------------------------- */

static void proto_uart_write_locked(const uint8_t *data, uint16_t len)
{
    uint16_t i;

    if ((data == NULL) || (len == 0U)) {
        return;
    }
    for (i = 0U; i < len; i++) {
        UART_Putc((char)data[i]);
    }
}

static void proto_send_frame(uint16_t cmd, uint8_t seq, const uint8_t *payload, uint16_t len,
                             uint8_t extra_flags)
{
    uint8_t *body;
    uint16_t crc;
    uint16_t total;
    uint8_t flags = (uint8_t)(PROTO_FLAG_DIR_DEVICE | extra_flags);

    if (len > PROTO_MAX_PAYLOAD) {
        return;
    }

    s_tx_frame[0] = PROTO_SOF0;
    s_tx_frame[1] = PROTO_SOF1;
    body = &s_tx_frame[2];
    body[0] = PROTO_VER;
    body[1] = flags;
    proto_put_u16(&body[2], len);
    proto_put_u16(&body[4], cmd);
    body[6] = seq;
    if ((len > 0U) && (payload != NULL)) {
        (void)memcpy(&body[7], payload, len);
    }

    crc = proto_crc16_ccitt_false(body, (uint16_t)(PROTO_HEADER_SIZE + len));
    proto_put_u16(&body[7 + len], crc);
    total = (uint16_t)(2U + PROTO_HEADER_SIZE + len + 2U);

    if (s_tx_mutex != NULL) {
        (void)xSemaphoreTake(s_tx_mutex, pdMS_TO_TICKS(100U));
    }
    proto_uart_write_locked(s_tx_frame, total);
    if (s_tx_mutex != NULL) {
        (void)xSemaphoreGive(s_tx_mutex);
    }
}

static void proto_reply_ack(uint16_t cmd, uint8_t seq, const uint8_t *payload, uint16_t len)
{
    proto_send_frame((uint16_t)(cmd | PROTO_RESPONSE_BIT), seq, payload, len, 0U);
}

static void proto_reply_nak(uint16_t cmd, uint8_t seq, uint8_t err_code)
{
    proto_send_frame((uint16_t)(cmd | PROTO_RESPONSE_BIT), seq, &err_code, 1U, PROTO_FLAG_NAK);
}

static void proto_push_frame(const uint8_t *payload, uint16_t len)
{
    proto_send_frame(PROTO_CMD_TELEMETRY_PUSH, 0U, payload, len, PROTO_FLAG_UNSOLICITED);
}

/* -------------------------------------------------------------------------- */
/* 参数写入适配                                                               */
/* -------------------------------------------------------------------------- */

static status_t proto_write_fw_version(const uint8_t *data, uint16_t len)
{
    char ver[NVS_CFG_FW_VER_MAX];

    if ((data == NULL) || (len == 0U) || (len >= NVS_CFG_FW_VER_MAX)) {
        return STATUS_INVALID_ARG;
    }
    (void)memcpy(ver, data, len);
    ver[len] = '\0';
    return nvs_param_set_fw_version(ver, NVS_WRITE_SRC_PROTOCOL);
}

static status_t proto_write_pid_speed(const uint8_t *data, uint16_t len)
{
    nvs_pid3_t pid;

    if ((data == NULL) || (len != sizeof(nvs_pid3_t))) {
        return STATUS_INVALID_ARG;
    }
    (void)memcpy(&pid, data, sizeof(pid));
    return nvs_param_set_pid_speed(&pid, NVS_WRITE_SRC_PROTOCOL);
}

static status_t proto_write_pid_line(const uint8_t *data, uint16_t len)
{
    nvs_pid3_t pid;

    if ((data == NULL) || (len != sizeof(nvs_pid3_t))) {
        return STATUS_INVALID_ARG;
    }
    (void)memcpy(&pid, data, sizeof(pid));
    return nvs_param_set_pid_line(&pid, NVS_WRITE_SRC_PROTOCOL);
}

static status_t proto_write_spd_limit(const uint8_t *data, uint16_t len)
{
    nvs_spd_limit_t limit;

    if ((data == NULL) || (len != sizeof(nvs_spd_limit_t))) {
        return STATUS_INVALID_ARG;
    }
    (void)memcpy(&limit, data, sizeof(limit));
    return nvs_param_set_spd_limit(&limit, NVS_WRITE_SRC_PROTOCOL);
}

static status_t proto_write_kinematics(const uint8_t *data, uint16_t len)
{
    nvs_kinematics_t kinem;

    if ((data == NULL) || (len != sizeof(nvs_kinematics_t))) {
        return STATUS_INVALID_ARG;
    }
    (void)memcpy(&kinem, data, sizeof(kinem));
    return nvs_param_set_kinematics(&kinem, NVS_WRITE_SRC_PROTOCOL);
}

static status_t proto_write_motor_dir(const uint8_t *data, uint16_t len)
{
    u32_t mask;

    if ((data == NULL) || (len != sizeof(u32_t))) {
        return STATUS_INVALID_ARG;
    }
    (void)memcpy(&mask, data, sizeof(mask));
    return nvs_param_set_motor_dir_mask(mask, NVS_WRITE_SRC_PROTOCOL);
}

static status_t proto_write_imu_offset(const uint8_t *data, uint16_t len)
{
    nvs_imu_offset_t offset;

    if ((data == NULL) || (len != sizeof(nvs_imu_offset_t))) {
        return STATUS_INVALID_ARG;
    }
    (void)memcpy(&offset, data, sizeof(offset));
    return nvs_param_set_imu_offset(&offset, NVS_WRITE_SRC_PROTOCOL);
}

static status_t proto_write_line_threshold(const uint8_t *data, uint16_t len)
{
    nvs_line_threshold_t threshold;

    if ((data == NULL) || (len != sizeof(nvs_line_threshold_t))) {
        return STATUS_INVALID_ARG;
    }
    (void)memcpy(&threshold, data, sizeof(threshold));
    return nvs_param_set_line_threshold(&threshold, NVS_WRITE_SRC_PROTOCOL);
}

static status_t proto_write_encoder_zero(const uint8_t *data, uint16_t len)
{
    nvs_encoder_zero_t zero;

    if ((data == NULL) || (len != sizeof(nvs_encoder_zero_t))) {
        return STATUS_INVALID_ARG;
    }
    (void)memcpy(&zero, data, sizeof(zero));
    return nvs_param_set_encoder_zero(&zero, NVS_WRITE_SRC_PROTOCOL);
}

static status_t proto_write_battery_cal(const uint8_t *data, uint16_t len)
{
    nvs_battery_cal_t cal;

    if ((data == NULL) || (len != sizeof(nvs_battery_cal_t))) {
        return STATUS_INVALID_ARG;
    }
    (void)memcpy(&cal, data, sizeof(cal));
    return nvs_param_set_battery_cal(&cal, NVS_WRITE_SRC_PROTOCOL);
}

static const proto_param_desc_t s_param_table[] = {
    { NVS_PARAM_SCHEMA,         (uint8_t)sizeof(u32_t),              NULL },
    { NVS_PARAM_SERIAL,       NVS_CFG_SERIAL_MAX,                  NULL },
    { NVS_PARAM_HW_REV,       (uint8_t)sizeof(u32_t),              NULL },
    { NVS_PARAM_BOOT_COUNT,   (uint8_t)sizeof(u32_t),              NULL },
    { NVS_PARAM_FW_VERSION,   NVS_CFG_FW_VER_MAX,                  proto_write_fw_version },
    { NVS_PARAM_PID_SPEED,    (uint8_t)sizeof(nvs_pid3_t),         proto_write_pid_speed },
    { NVS_PARAM_PID_LINE,     (uint8_t)sizeof(nvs_pid3_t),         proto_write_pid_line },
    { NVS_PARAM_SPD_LIMIT,    (uint8_t)sizeof(nvs_spd_limit_t),    proto_write_spd_limit },
    { NVS_PARAM_KINEMATICS,   (uint8_t)sizeof(nvs_kinematics_t),  proto_write_kinematics },
    { NVS_PARAM_MOTOR_DIR,    (uint8_t)sizeof(u32_t),              proto_write_motor_dir },
    { NVS_PARAM_IMU_OFFSET,   (uint8_t)sizeof(nvs_imu_offset_t),   proto_write_imu_offset },
    { NVS_PARAM_LINE_THRESHOLD, (uint8_t)sizeof(nvs_line_threshold_t), proto_write_line_threshold },
    { NVS_PARAM_ENCODER_ZERO, (uint8_t)sizeof(nvs_encoder_zero_t), proto_write_encoder_zero },
    { NVS_PARAM_BATTERY_CAL,  (uint8_t)sizeof(nvs_battery_cal_t), proto_write_battery_cal },
    { NVS_PARAM_LAST_MODE,    (uint8_t)sizeof(u32_t),              NULL },
};

#define PROTO_PARAM_TABLE_COUNT ((uint16_t)(sizeof(s_param_table) / sizeof(s_param_table[0])))

static const proto_param_desc_t *proto_param_lookup(nvs_param_id_t id)
{
    uint16_t i;

    for (i = 0U; i < PROTO_PARAM_TABLE_COUNT; i++) {
        if (s_param_table[i].id == id) {
            return &s_param_table[i];
        }
    }
    return NULL;
}

static uint16_t proto_param_read_blob(nvs_param_id_t id, uint8_t *out, uint16_t out_max)
{
    const nvs_cfg_t *cfg = nvs_cfg_get();
    const proto_param_desc_t *desc = proto_param_lookup(id);

    if ((cfg == NULL) || (desc == NULL) || (out == NULL) || (out_max < desc->size)) {
        return 0U;
    }

    switch (id) {
    case NVS_PARAM_SCHEMA:
        (void)memcpy(out, &cfg->schema_version, sizeof(cfg->schema_version));
        return (uint16_t)sizeof(cfg->schema_version);
    case NVS_PARAM_SERIAL:
        (void)memcpy(out, cfg->serial, desc->size);
        return desc->size;
    case NVS_PARAM_HW_REV:
        (void)memcpy(out, &cfg->hw_rev, sizeof(cfg->hw_rev));
        return (uint16_t)sizeof(cfg->hw_rev);
    case NVS_PARAM_BOOT_COUNT:
        (void)memcpy(out, &cfg->boot_count, sizeof(cfg->boot_count));
        return (uint16_t)sizeof(cfg->boot_count);
    case NVS_PARAM_FW_VERSION:
        (void)memcpy(out, cfg->fw_version, desc->size);
        return desc->size;
    case NVS_PARAM_PID_SPEED:
        (void)memcpy(out, &cfg->pid_speed, sizeof(cfg->pid_speed));
        return (uint16_t)sizeof(cfg->pid_speed);
    case NVS_PARAM_PID_LINE:
        (void)memcpy(out, &cfg->pid_line, sizeof(cfg->pid_line));
        return (uint16_t)sizeof(cfg->pid_line);
    case NVS_PARAM_SPD_LIMIT:
        (void)memcpy(out, &cfg->spd_limit, sizeof(cfg->spd_limit));
        return (uint16_t)sizeof(cfg->spd_limit);
    case NVS_PARAM_KINEMATICS:
        (void)memcpy(out, &cfg->kinematics, sizeof(cfg->kinematics));
        return (uint16_t)sizeof(cfg->kinematics);
    case NVS_PARAM_MOTOR_DIR:
        (void)memcpy(out, &cfg->motor_dir_mask, sizeof(cfg->motor_dir_mask));
        return (uint16_t)sizeof(cfg->motor_dir_mask);
    case NVS_PARAM_IMU_OFFSET:
        (void)memcpy(out, &cfg->imu_offset, sizeof(cfg->imu_offset));
        return (uint16_t)sizeof(cfg->imu_offset);
    case NVS_PARAM_LINE_THRESHOLD:
        (void)memcpy(out, &cfg->line_threshold, sizeof(cfg->line_threshold));
        return (uint16_t)sizeof(cfg->line_threshold);
    case NVS_PARAM_ENCODER_ZERO:
        (void)memcpy(out, &cfg->encoder_zero, sizeof(cfg->encoder_zero));
        return (uint16_t)sizeof(cfg->encoder_zero);
    case NVS_PARAM_BATTERY_CAL:
        (void)memcpy(out, &cfg->battery_cal, sizeof(cfg->battery_cal));
        return (uint16_t)sizeof(cfg->battery_cal);
    case NVS_PARAM_LAST_MODE:
        (void)memcpy(out, &cfg->last_mode, sizeof(cfg->last_mode));
        return (uint16_t)sizeof(cfg->last_mode);
    default:
        return 0U;
    }
}

/* -------------------------------------------------------------------------- */
/* 遥测                                                                       */
/* -------------------------------------------------------------------------- */

static void proto_sample_line_adc(uint16_t *out, size_t count)
{
    size_t i;

    if ((out == NULL) || (count == 0U)) {
        return;
    }
    for (i = 0U; i < count; i++) {
        out[i] = 0U;
    }
    if (!device_profile_board_wants(DEVICE_BOARD_MASK_LINE)) {
        return;
    }
    (void)Line_Sample(out, count);
}

static uint16_t proto_sample_ultrasonic_mm(void)
{
    uint16_t mm = PROTO_ULTRA_INVALID_MM;
    status_t st;

    if (!device_profile_board_wants(DEVICE_BOARD_MASK_PERIPH)) {
        return PROTO_ULTRA_INVALID_MM;
    }

    /* PC1=Echo/SWDIO：boot 灯效结束后再 init，避免占用 SWD 影响烧录 */
    if (s_ultra_init_attempted == FALSE) {
        if (led_scene_is_active()) {
            return PROTO_ULTRA_INVALID_MM;
        }
        s_ultra_init_attempted = TRUE;
        if (ultrasonic_init() != STATUS_OK) {
            return PROTO_ULTRA_INVALID_MM;
        }
        s_ultra_ready = TRUE;
    }

    if (s_ultra_ready == FALSE) {
        return PROTO_ULTRA_INVALID_MM;
    }

    st = ultrasonic_measure_mm(&mm);
    if (st != STATUS_OK) {
        return PROTO_ULTRA_INVALID_MM;
    }
    return mm;
}

static uint16_t proto_build_telemetry(uint8_t *out, uint16_t out_max)
{
    battery_voltage_t batt = {0};
    battery_info_t info = {0};
    attitude_euler_t euler = {0};
    uint16_t line_adc[PROTO_LINE_ADC_COUNT];
    int32_t enc[PROTO_ENCODER_COUNT];
    uint8_t batt_pct = 0U;
    uint16_t batt_mv = 0U;
    float roll = 0.0f;
    float pitch = 0.0f;
    float yaw = 0.0f;
    uint8_t i;
    const uint16_t need = (uint16_t)(2U + 1U + 12U + (uint16_t)(4 * PROTO_ENCODER_COUNT) +
                                     (uint16_t)(2U * PROTO_LINE_ADC_COUNT) + 4U + 2U);

    if ((out == NULL) || (out_max < need)) {
        return 0U;
    }

    (void)battery_percent_update();
    (void)battery_info_read(&info, &batt);
    batt_mv = batt.current_mv;
    batt_pct = info.percent;

    if (attitude_is_ready() && (attitude_get_euler(&euler) == STATUS_OK)) {
        roll = (float)euler.roll_x10 / 10.0f;
        pitch = (float)euler.pitch_x10 / 10.0f;
        yaw = (float)euler.yaw_x10 / 10.0f;
    }

    for (i = 0U; i < PROTO_ENCODER_COUNT; i++) {
        enc[i] = cfg_encoder_count(i);
    }
    proto_sample_line_adc(line_adc, PROTO_LINE_ADC_COUNT);

    proto_put_u16(&out[0], batt_mv);
    out[2] = batt_pct;
    proto_put_f32(&out[3], roll);
    proto_put_f32(&out[7], pitch);
    proto_put_f32(&out[11], yaw);
    for (i = 0U; i < PROTO_ENCODER_COUNT; i++) {
        proto_put_u32(&out[15U + (i * 4U)], (uint32_t)enc[i]);
    }
    {
        const uint16_t line_off = (uint16_t)(15U + (4U * PROTO_ENCODER_COUNT));
        const uint16_t uptime_off = (uint16_t)(line_off + (2U * PROTO_LINE_ADC_COUNT));

        for (i = 0U; i < PROTO_LINE_ADC_COUNT; i++) {
            proto_put_u16(&out[line_off + (i * 2U)], line_adc[i]);
        }
        proto_put_u32(&out[uptime_off], proto_uptime_ms());
        proto_put_u16(&out[uptime_off + 4U], proto_sample_ultrasonic_mm());
    }
    return need;
}

static uint8_t proto_effective_hz(uint8_t hz, uint8_t default_hz)
{
    return (hz == 0U) ? default_hz : hz;
}

static uint32_t proto_period_ms_for(uint8_t hz, uint8_t default_hz)
{
    uint8_t eff = proto_effective_hz(hz, default_hz);

    if (eff == 0U) {
        return 1000U;
    }
    return 1000U / (uint32_t)eff;
}

static void proto_push_attitude(void)
{
    attitude_euler_t euler = {0};
    uint8_t payload[5U + 12U];
    float roll = 0.0f;
    float pitch = 0.0f;
    float yaw = 0.0f;

    if (attitude_is_ready()) {
        (void)attitude_get_euler(&euler);
        roll = (float)euler.roll_x10 / 10.0f;
        pitch = (float)euler.pitch_x10 / 10.0f;
        yaw = (float)euler.yaw_x10 / 10.0f;
    }

    payload[0] = PROTO_PUSH_CH_ATTITUDE;
    proto_put_u32(&payload[1], proto_uptime_ms());
    proto_put_f32(&payload[5], roll);
    proto_put_f32(&payload[9], pitch);
    proto_put_f32(&payload[13], yaw);
    proto_push_frame(payload, (uint16_t)sizeof(payload));
}

static void proto_push_line_adc(void)
{
    uint8_t payload[5U + (2U * PROTO_LINE_ADC_COUNT)];
    uint16_t line_adc[PROTO_LINE_ADC_COUNT];
    uint8_t i;

    proto_sample_line_adc(line_adc, PROTO_LINE_ADC_COUNT);
    payload[0] = PROTO_PUSH_CH_LINE_ADC;
    proto_put_u32(&payload[1], proto_uptime_ms());
    for (i = 0U; i < PROTO_LINE_ADC_COUNT; i++) {
        proto_put_u16(&payload[5U + (i * 2U)], line_adc[i]);
    }
    proto_push_frame(payload, (uint16_t)sizeof(payload));
}

static void proto_push_encoder(void)
{
    uint8_t payload[5U + (4U * PROTO_ENCODER_COUNT)];
    uint8_t i;

    payload[0] = PROTO_PUSH_CH_ENCODER;
    proto_put_u32(&payload[1], proto_uptime_ms());
    for (i = 0U; i < PROTO_ENCODER_COUNT; i++) {
        proto_put_u32(&payload[5U + (i * 4U)], (uint32_t)cfg_encoder_count(i));
    }
    proto_push_frame(payload, (uint16_t)sizeof(payload));
}

static void proto_push_ultrasonic(void)
{
    uint8_t payload[5U + 2U];
    uint16_t mm = proto_sample_ultrasonic_mm();

    payload[0] = PROTO_PUSH_CH_ULTRASONIC;
    proto_put_u32(&payload[1], proto_uptime_ms());
    proto_put_u16(&payload[5], mm);
    proto_push_frame(payload, (uint16_t)sizeof(payload));
}

/* -------------------------------------------------------------------------- */
/* 遥控                                                                       */
/* -------------------------------------------------------------------------- */

static void proto_motor_all_stop(void)
{
    uint8_t i;

    if (!device_profile_board_wants(DEVICE_BOARD_MASK_MOTOR)) {
        return;
    }
    for (i = 1U; i <= 4U; i++) {
        Motor_SetSpeed(i, 0);
    }
}

static void proto_apply_drive(int16_t throttle, int16_t steer)
{
    const nvs_spd_limit_t *lim;
    int32_t base_rpm;
    int32_t turn_rpm;
    int32_t left;
    int32_t right;

    if (!device_profile_board_wants(DEVICE_BOARD_MASK_MOTOR)) {
        return;
    }

    lim = cfg_spd_limit();
    base_rpm = (int32_t)(((float)throttle / (float)PROTO_DRIVE_THROTTLE_MAX) * lim->max_rpm);
    turn_rpm = (int32_t)(((float)steer / (float)PROTO_DRIVE_STEER_MAX) * lim->max_rpm * 0.5f);
    left = base_rpm - turn_rpm;
    right = base_rpm + turn_rpm;

    Motor_SetSpeed(1U, cfg_motor_rpm(1U, left));
    Motor_SetSpeed(2U, cfg_motor_rpm(2U, right));
    Motor_SetSpeed(3U, cfg_motor_rpm(3U, left));
    Motor_SetSpeed(4U, cfg_motor_rpm(4U, right));
}

static void proto_drive_stop_internal(void)
{
    s_drive_active = false;
    s_drive_stop_req = true;
}

/* -------------------------------------------------------------------------- */
/* 命令处理                                                                   */
/* -------------------------------------------------------------------------- */

static uint32_t proto_caps(void)
{
    uint32_t caps = PROTO_CAP_TELEMETRY | PROTO_CAP_PARAM_RW | PROTO_CAP_SUBSCRIBE;

    if (device_profile_board_wants(DEVICE_BOARD_MASK_MOTOR)) {
        caps |= PROTO_CAP_DRIVE;
    }
    return caps;
}

static void proto_handle_hello(uint8_t seq)
{
    const nvs_cfg_t *cfg = nvs_cfg_get();
    uint8_t payload[1U + NVS_CFG_FW_VER_MAX + 8U];

    payload[0] = PROTO_VER;
    if (cfg != NULL) {
        (void)memcpy(&payload[1], cfg->fw_version, NVS_CFG_FW_VER_MAX);
        proto_put_u32(&payload[1U + NVS_CFG_FW_VER_MAX], cfg->hw_rev);
    } else {
        (void)memset(&payload[1], 0, NVS_CFG_FW_VER_MAX + 4U);
    }
    proto_put_u32(&payload[1U + NVS_CFG_FW_VER_MAX + 4U], proto_caps());
    proto_reply_ack(PROTO_CMD_HELLO, seq, payload, (uint16_t)sizeof(payload));
}

static void proto_handle_ping(uint8_t seq)
{
    uint8_t payload[4];

    proto_put_u32(payload, proto_uptime_ms());
    proto_reply_ack(PROTO_CMD_PING, seq, payload, (uint16_t)sizeof(payload));
}

static void proto_handle_get_telemetry(uint8_t seq)
{
    uint8_t payload[64];
    uint16_t len = proto_build_telemetry(payload, (uint16_t)sizeof(payload));

    if (len == 0U) {
        proto_reply_nak(PROTO_CMD_GET_TELEMETRY, seq, PROTO_ERR_BUSY);
        return;
    }
    proto_reply_ack(PROTO_CMD_GET_TELEMETRY, seq, payload, len);
}

static void proto_handle_subscribe(uint8_t seq, const uint8_t *payload, uint16_t len)
{
    if (len < 8U) {
        proto_reply_nak(PROTO_CMD_SUBSCRIBE, seq, PROTO_ERR_BAD_LEN);
        return;
    }

    s_sub.mask = proto_get_u32(&payload[0]);
    s_sub.hz_att = payload[4];
    s_sub.hz_enc = payload[5];
    s_sub.hz_line = payload[6];
    s_sub.hz_ultra = payload[7];
    s_sub.acc_att_ms = 0U;
    s_sub.acc_enc_ms = 0U;
    s_sub.acc_line_ms = 0U;
    s_sub.acc_ultra_ms = 0U;
    proto_reply_ack(PROTO_CMD_SUBSCRIBE, seq, NULL, 0U);
}

static void proto_handle_unsubscribe(uint8_t seq, const uint8_t *payload, uint16_t len)
{
    uint32_t mask;

    if (len < 4U) {
        proto_reply_nak(PROTO_CMD_UNSUBSCRIBE, seq, PROTO_ERR_BAD_LEN);
        return;
    }
    mask = proto_get_u32(payload);
    s_sub.mask &= ~mask;
    proto_reply_ack(PROTO_CMD_UNSUBSCRIBE, seq, NULL, 0U);
}

static void proto_handle_param_list(uint8_t seq)
{
    uint8_t payload[PROTO_PARAM_TABLE_COUNT * 8U];
    uint16_t off = 0U;
    uint16_t i;

    for (i = 0U; i < PROTO_PARAM_TABLE_COUNT; i++) {
        uint8_t flags = 0U;

        if (nvs_param_write_allowed(s_param_table[i].id, NVS_WRITE_SRC_PROTOCOL)) {
            flags = 1U;
        }
        proto_put_u16(&payload[off], (uint16_t)s_param_table[i].id);
        payload[off + 2U] = flags;
        payload[off + 3U] = s_param_table[i].size;
        payload[off + 4U] = 0U;
        payload[off + 5U] = 0U;
        off = (uint16_t)(off + 8U);
    }
    proto_reply_ack(PROTO_CMD_PARAM_LIST, seq, payload, off);
}

static void proto_handle_param_read(uint8_t seq, const uint8_t *payload, uint16_t len)
{
    nvs_param_id_t id;
    uint8_t out[128];
    uint16_t out_len;

    if (len < 2U) {
        proto_reply_nak(PROTO_CMD_PARAM_READ, seq, PROTO_ERR_BAD_LEN);
        return;
    }
    id = (nvs_param_id_t)proto_get_u16(payload);
    if (proto_param_lookup(id) == NULL) {
        proto_reply_nak(PROTO_CMD_PARAM_READ, seq, PROTO_ERR_PARAM_ID_INVALID);
        return;
    }
    out_len = proto_param_read_blob(id, out, (uint16_t)sizeof(out));
    if (out_len == 0U) {
        proto_reply_nak(PROTO_CMD_PARAM_READ, seq, PROTO_ERR_PARAM_ID_INVALID);
        return;
    }
    proto_reply_ack(PROTO_CMD_PARAM_READ, seq, out, out_len);
}

static void proto_handle_param_write(uint8_t seq, const uint8_t *payload, uint16_t len)
{
    nvs_param_id_t id;
    const proto_param_desc_t *desc;
    status_t st;

    if (len < 3U) {
        proto_reply_nak(PROTO_CMD_PARAM_WRITE, seq, PROTO_ERR_BAD_LEN);
        return;
    }
    id = (nvs_param_id_t)proto_get_u16(payload);
    desc = proto_param_lookup(id);
    if (desc == NULL) {
        proto_reply_nak(PROTO_CMD_PARAM_WRITE, seq, PROTO_ERR_PARAM_ID_INVALID);
        return;
    }
    if ((desc->write_fn == NULL) ||
        !nvs_param_write_allowed(id, NVS_WRITE_SRC_PROTOCOL)) {
        proto_reply_nak(PROTO_CMD_PARAM_WRITE, seq, PROTO_ERR_PARAM_READ_ONLY);
        return;
    }
    if ((uint16_t)(len - 2U) != desc->size) {
        proto_reply_nak(PROTO_CMD_PARAM_WRITE, seq, PROTO_ERR_BAD_LEN);
        return;
    }

    st = desc->write_fn(&payload[2], desc->size);
    if (st == STATUS_INVALID_ARG) {
        proto_reply_nak(PROTO_CMD_PARAM_WRITE, seq, PROTO_ERR_PARAM_VALUE_INVALID);
        return;
    }
    if (st != STATUS_OK) {
        if (st == STATUS_NOT_SUPPORTED) {
            proto_reply_nak(PROTO_CMD_PARAM_WRITE, seq, PROTO_ERR_PARAM_READ_ONLY);
        } else {
            proto_reply_nak(PROTO_CMD_PARAM_WRITE, seq, PROTO_ERR_NVS_WRITE_FAIL);
        }
        return;
    }
    proto_reply_ack(PROTO_CMD_PARAM_WRITE, seq, NULL, 0U);
}

static void proto_handle_drive(uint8_t seq, const uint8_t *payload, uint16_t len)
{
    int16_t throttle;
    int16_t steer;

    if ((proto_caps() & PROTO_CAP_DRIVE) == 0U) {
        proto_reply_nak(PROTO_CMD_DRIVE, seq, PROTO_ERR_UNSUPPORTED);
        return;
    }
    if (len < 4U) {
        proto_reply_nak(PROTO_CMD_DRIVE, seq, PROTO_ERR_BAD_LEN);
        return;
    }

    throttle = proto_get_i16(&payload[0]);
    steer = proto_get_i16(&payload[2]);
    if ((throttle < -PROTO_DRIVE_THROTTLE_MAX) || (throttle > PROTO_DRIVE_THROTTLE_MAX) ||
        (steer < -PROTO_DRIVE_STEER_MAX) || (steer > PROTO_DRIVE_STEER_MAX)) {
        proto_reply_nak(PROTO_CMD_DRIVE, seq, PROTO_ERR_PARAM_VALUE_INVALID);
        return;
    }

    s_drive_throttle = throttle;
    s_drive_steer = steer;
    s_drive_last_ms = proto_uptime_ms();
    s_drive_active = true;
    s_drive_stop_req = false;
    proto_reply_ack(PROTO_CMD_DRIVE, seq, NULL, 0U);
}

static void proto_handle_drive_stop(uint8_t seq)
{
    if ((proto_caps() & PROTO_CAP_DRIVE) == 0U) {
        proto_reply_nak(PROTO_CMD_DRIVE_STOP, seq, PROTO_ERR_UNSUPPORTED);
        return;
    }
    proto_drive_stop_internal();
    proto_reply_ack(PROTO_CMD_DRIVE_STOP, seq, NULL, 0U);
}

static void proto_dispatch(uint16_t cmd, uint8_t seq, const uint8_t *payload, uint16_t len)
{
    switch (cmd) {
    case PROTO_CMD_HELLO:
        proto_handle_hello(seq);
        break;
    case PROTO_CMD_PING:
        proto_handle_ping(seq);
        break;
    case PROTO_CMD_GET_TELEMETRY:
        proto_handle_get_telemetry(seq);
        break;
    case PROTO_CMD_SUBSCRIBE:
        proto_handle_subscribe(seq, payload, len);
        break;
    case PROTO_CMD_UNSUBSCRIBE:
        proto_handle_unsubscribe(seq, payload, len);
        break;
    case PROTO_CMD_PARAM_LIST:
        proto_handle_param_list(seq);
        break;
    case PROTO_CMD_PARAM_READ:
        proto_handle_param_read(seq, payload, len);
        break;
    case PROTO_CMD_PARAM_WRITE:
        proto_handle_param_write(seq, payload, len);
        break;
    case PROTO_CMD_DRIVE:
        proto_handle_drive(seq, payload, len);
        break;
    case PROTO_CMD_DRIVE_STOP:
        proto_handle_drive_stop(seq);
        break;
    default:
        proto_reply_nak(cmd, seq, PROTO_ERR_UNKNOWN_CMD);
        break;
    }
}

/* -------------------------------------------------------------------------- */
/* 帧解析                                                                     */
/* -------------------------------------------------------------------------- */

static void proto_parse_reset(void)
{
    s_parser.state = PROTO_PARSE_SOF0;
    s_parser.body_len = 0U;
}

static void proto_parse_byte(uint8_t byte)
{
    uint16_t payload_len;
    uint16_t crc_expected;
    uint16_t crc_actual;
    uint16_t cmd;
    uint8_t seq;
    const uint8_t *payload;

    switch (s_parser.state) {
    case PROTO_PARSE_SOF0:
        if (byte == PROTO_SOF0) {
            s_parser.state = PROTO_PARSE_SOF1;
        }
        break;
    case PROTO_PARSE_SOF1:
        if (byte == PROTO_SOF1) {
            s_parser.body_len = 0U;
            s_parser.state = PROTO_PARSE_BODY;
        } else if (byte == PROTO_SOF0) {
            s_parser.state = PROTO_PARSE_SOF1;
        } else {
            s_parser.state = PROTO_PARSE_SOF0;
        }
        break;
    case PROTO_PARSE_BODY:
        s_parser.body[s_parser.body_len++] = byte;
        if (s_parser.body_len < (PROTO_HEADER_SIZE + 2U)) {
            break;
        }
        payload_len = proto_get_u16(&s_parser.body[2]);
        if (payload_len > PROTO_MAX_PAYLOAD) {
            proto_parse_reset();
            break;
        }
        if (s_parser.body_len < (uint16_t)(PROTO_HEADER_SIZE + payload_len + 2U)) {
            break;
        }

        crc_expected = proto_get_u16(&s_parser.body[PROTO_HEADER_SIZE + payload_len]);
        crc_actual = proto_crc16_ccitt_false(s_parser.body,
                                             (uint16_t)(PROTO_HEADER_SIZE + payload_len));
        if (crc_actual != crc_expected) {
            proto_parse_reset();
            break;
        }

        if (s_parser.body[1] & PROTO_FLAG_DIR_DEVICE) {
            proto_parse_reset();
            break;
        }

        cmd = proto_get_u16(&s_parser.body[4]);
        seq = s_parser.body[6];
        payload = (payload_len > 0U) ? &s_parser.body[PROTO_HEADER_SIZE] : NULL;
        proto_dispatch(cmd, seq, payload, payload_len);
        proto_parse_reset();
        break;
    default:
        proto_parse_reset();
        break;
    }
}

/* -------------------------------------------------------------------------- */
/* RX 任务                                                                    */
/* -------------------------------------------------------------------------- */

static void proto_rx_task(void *arg)
{
    static bool s_stack_logged;

    (void)arg;

    for (;;) {
        if (!s_stack_logged) {
            s_stack_logged = true;
            LOG_INFO("proto: stack hw proto_rx=%u words",
                     (unsigned)uxTaskGetStackHighWaterMark(NULL));
        }

        char ch;

        if (UART_Getc(&ch) == 0) {
            vTaskDelay(pdMS_TO_TICKS(PROTO_RX_POLL_MS));
            continue;
        }
        if (s_echo_mode) {
            UART_Putc(ch);
            continue;
        }
        proto_parse_byte((uint8_t)ch);
    }
}

void proto_telemetry_tick(uint32_t period_ms)
{
    if (s_drive_stop_req) {
        proto_motor_all_stop();
        s_drive_stop_req = false;
        s_drive_active = false;
    } else if (s_drive_active) {
        uint32_t now = proto_uptime_ms();

        if ((now - s_drive_last_ms) > PROTO_DRIVE_TIMEOUT_MS) {
            proto_motor_all_stop();
            s_drive_active = false;
        } else {
            proto_apply_drive(s_drive_throttle, s_drive_steer);
        }
    }

    if (s_sub.mask == 0U) {
        return;
    }

    if ((s_sub.mask & PROTO_CH_ATTITUDE) != 0U) {
        s_sub.acc_att_ms += period_ms;
        if (s_sub.acc_att_ms >= proto_period_ms_for(s_sub.hz_att, PROTO_DEFAULT_HZ_ATT)) {
            s_sub.acc_att_ms = 0U;
            proto_push_attitude();
        }
    }
    if ((s_sub.mask & PROTO_CH_ENCODER) != 0U) {
        s_sub.acc_enc_ms += period_ms;
        if (s_sub.acc_enc_ms >= proto_period_ms_for(s_sub.hz_enc, PROTO_DEFAULT_HZ_ENC)) {
            s_sub.acc_enc_ms = 0U;
            proto_push_encoder();
        }
    }
    if ((s_sub.mask & PROTO_CH_LINE_ADC) != 0U) {
        s_sub.acc_line_ms += period_ms;
        if (s_sub.acc_line_ms >= proto_period_ms_for(s_sub.hz_line, PROTO_DEFAULT_HZ_LINE)) {
            s_sub.acc_line_ms = 0U;
            proto_push_line_adc();
        }
    }
    if ((s_sub.mask & PROTO_CH_ULTRASONIC) != 0U) {
        s_sub.acc_ultra_ms += period_ms;
        if (s_sub.acc_ultra_ms >= proto_period_ms_for(s_sub.hz_ultra, PROTO_DEFAULT_HZ_ULTRA)) {
            s_sub.acc_ultra_ms = 0U;
            proto_push_ultrasonic();
        }
    }
}

status_t proto_uart_service_start(void)
{
    if (s_rx_task != NULL) {
        return STATUS_OK;
    }

    if (s_tx_mutex == NULL) {
        s_tx_mutex = xSemaphoreCreateMutex();
        if (s_tx_mutex == NULL) {
            return STATUS_NO_MEM;
        }
    }

    proto_parse_reset();
    (void)memset(&s_sub, 0, sizeof(s_sub));
    s_sub.hz_att = PROTO_DEFAULT_HZ_ATT;
    s_sub.hz_line = PROTO_DEFAULT_HZ_LINE;
    s_sub.hz_enc = PROTO_DEFAULT_HZ_ENC;
    s_sub.hz_ultra = PROTO_DEFAULT_HZ_ULTRA;
    s_ultra_ready = FALSE;
    s_ultra_init_attempted = FALSE;

    if (xTaskCreate(proto_rx_task, PROTO_TASK_NAME_RX, PROTO_RX_STACK_WORDS, NULL,
                    PROTO_RX_PRIORITY, &s_rx_task) != pdPASS) {
        LOG_ERROR("proto: create rx task failed");
        vSemaphoreDelete(s_tx_mutex);
        s_tx_mutex = NULL;
        s_rx_task = NULL;
        return STATUS_FAIL;
    }

    LOG_INFO("proto: uart0 service ready");
    return STATUS_OK;
}

void proto_echo_set(bool_t enable)
{
    s_echo_mode = enable;
    if (enable) {
        proto_parse_reset();
    }
    LOG_INFO("proto: echo %s", enable ? "on" : "off");
}

bool_t proto_echo_get(void)
{
    return s_echo_mode;
}

void proto_send_raw(const uint8_t *data, uint16_t len)
{
    uint16_t i;

    if ((data == NULL) || (len == 0U)) {
        return;
    }
    if (s_tx_mutex != NULL) {
        (void)xSemaphoreTake(s_tx_mutex, pdMS_TO_TICKS(100U));
    }
    for (i = 0U; i < len; i++) {
        UART_Putc((char)data[i]);
    }
    if (s_tx_mutex != NULL) {
        (void)xSemaphoreGive(s_tx_mutex);
    }
}
