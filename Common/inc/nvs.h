/**
 * @file    nvs.h
 * @brief   NVS：2×4 KB 页式 KV + 配置缓存与参数访问策略
 *
 * Boot 保留区（256 B @ offset 16）供 Bootloader 读写的激活元数据，不参与 KV 表。
 * 写入方：厂测 / CMD / 协议层（预留）/ 固件内部；见 nvs_write_src_t。
 */

#ifndef NVS_H
#define NVS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "type.h"

/* -------------------------------------------------------------------------- */
/* Flash 页布局                                                               */
/* -------------------------------------------------------------------------- */

#define NVS_PAGE_SIZE           4096U
#define NVS_PAGE_COUNT          2U
#define NVS_PAGE_HDR_SIZE       16U
#define NVS_BOOT_RSVD_OFFSET    NVS_PAGE_HDR_SIZE
#define NVS_BOOT_RSVD_SIZE      256U
#define NVS_DATA_OFFSET         (NVS_BOOT_RSVD_OFFSET + NVS_BOOT_RSVD_SIZE)

#define NVS_PAGE_MAGIC          0x4E565331U   /* "NVS1" */
#define NVS_PAGE_VERSION        1U

#define NVS_KEY_MAX             16U
#define NVS_NS_MAX              8U
#define NVS_BLOB_MAX            128U

typedef enum {
    NVS_FLASH_ALLOW_NVS = 1U << 0,
} nvs_flash_allow_t;

/* -------------------------------------------------------------------------- */
/* 配置 schema                                                                */
/* -------------------------------------------------------------------------- */

#define NVS_CFG_SCHEMA_VERSION      1U
#define NVS_CFG_SERIAL_MAX          16U
#define NVS_CFG_FW_VER_MAX          16U
#define NVS_CFG_LINE_SENSOR_COUNT   6U
#define NVS_CFG_ENCODER_MAX         4U

#define NVS_CFG_NS_DEV              "dev"
#define NVS_CFG_NS_CTRL             "ctrl"
#define NVS_CFG_NS_CAL              "cal"
#define NVS_CFG_NS_USER             "user"

#define NVS_CFG_KEY_SCHEMA          "schema"
#define NVS_CFG_KEY_SERIAL          "serial"
#define NVS_CFG_KEY_HW_REV          "hw_rev"
#define NVS_CFG_KEY_BOOT_CNT        "boot_cnt"
#define NVS_CFG_KEY_FW_VER          "fw_ver"
#define NVS_CFG_KEY_SPD_LIM         "spd_lim"
#define NVS_CFG_KEY_KINEM           "kinem"
#define NVS_CFG_KEY_MOT_DIR         "mot_dir"
#define NVS_CFG_KEY_ENC_DIR         "enc_dir"
#define NVS_CFG_KEY_IMU_OFF         "imu_off"
#define NVS_CFG_KEY_LINE_TH         "line_th"
#define NVS_CFG_KEY_LINE_POL        "line_pol"
#define NVS_CFG_KEY_LINE_BASE       "line_base"
#define NVS_CFG_KEY_ENC_ZERO        "enc_zero"
#define NVS_CFG_KEY_BAT_CAL         "bat_cal"
#define NVS_CFG_KEY_MAG_HDG         "mag_hdg"
#define NVS_CFG_KEY_LAST_MODE       "last_mode"

#define NVS_HW_REV_CAR_4WD_V1       0U
#define NVS_HW_REV_CAR_2WD_V1       1U
#define NVS_HW_REV_RC_V1            2U

/** 固件版本字符串默认值（可 -DFW_VERSION_STR=... 覆盖；≤15 字符） */
#ifndef FW_VERSION_STR
#define FW_VERSION_STR              "0.1.0"
#endif

/** 线协议 / NVS 固定为 u32，避免 enum 宽度随编译器变化 */
typedef u32_t nvs_run_mode_t;

enum {
    NVS_RUN_MODE_IDLE = 0,
    NVS_RUN_MODE_MANUAL = 1,
    NVS_RUN_MODE_LINE_FOLLOW = 2,
    NVS_RUN_MODE_REMOTE = 3,
};

typedef struct {
    f32_t kp;
    f32_t ki;
    f32_t kd;
} nvs_pid3_t;

typedef struct {
    f32_t max_rpm;
    f32_t max_accel_rpm_s;
} nvs_spd_limit_t;

typedef struct {
    f32_t wheel_diam_m;
    f32_t gear_ratio;
    f32_t track_width_m;
    f32_t wheelbase_m;
    u32_t encoder_cpr;
} nvs_kinematics_t;

typedef struct {
    f32_t gyro[3];
    f32_t accel[3];
} nvs_imu_offset_t;

typedef struct {
    /** 各路二值化阈值；出厂统一 2048，上位机可调 */
    u16_t threshold[NVS_CFG_LINE_SENSOR_COUNT];
} nvs_line_threshold_t;

typedef struct {
    s32_t zero[NVS_CFG_ENCODER_MAX];
} nvs_encoder_zero_t;

typedef struct {
    f32_t scale;
    f32_t offset_v;
} nvs_battery_cal_t;

typedef struct {
    u32_t schema_version;
    char serial[NVS_CFG_SERIAL_MAX];
    u32_t hw_rev;
    u32_t boot_count;
    char fw_version[NVS_CFG_FW_VER_MAX];
    nvs_pid3_t pid_speed;
    nvs_pid3_t pid_line;
    nvs_pid3_t pid_yaw;
    nvs_pid3_t pid_dist;
    nvs_spd_limit_t spd_limit;
    nvs_kinematics_t kinematics;
    u32_t motor_dir_mask;
    /** 编码器计数极性：bit0=M1 … bit3=M4，置位则翻转该路 delta/RPM 符号 */
    u32_t encoder_dir_mask;
    nvs_imu_offset_t imu_offset;
    nvs_line_threshold_t line_threshold;
    /**
     * 黑线电平：0=低电平为黑（ADC < threshold）；
     * 1=高电平为黑（ADC >= threshold，默认）
     */
    u8_t line_black_active_high;
    f32_t line_base_rpm;
    nvs_encoder_zero_t encoder_zero;
    nvs_battery_cal_t battery_cal;
    /** 磁力计航向零点偏移（度）；显示 yaw = FusionCompass - offset */
    f32_t mag_heading_offset_deg;
    nvs_run_mode_t last_mode;
} nvs_cfg_t;

/* -------------------------------------------------------------------------- */
/* 写入来源（协议层使用 NVS_WRITE_SRC_PROTOCOL）                                 */
/* -------------------------------------------------------------------------- */

typedef enum {
    NVS_WRITE_SRC_FACTORY = 0,
    NVS_WRITE_SRC_CMD,
    NVS_WRITE_SRC_PROTOCOL,
    NVS_WRITE_SRC_INTERNAL,
} nvs_write_src_t;

typedef enum {
    NVS_PARAM_SCHEMA = 0,
    NVS_PARAM_SERIAL,
    NVS_PARAM_HW_REV,
    NVS_PARAM_BOOT_COUNT,
    NVS_PARAM_FW_VERSION,
    NVS_PARAM_PID_SPEED,
    NVS_PARAM_PID_LINE,
    NVS_PARAM_SPD_LIMIT,
    NVS_PARAM_KINEMATICS,
    NVS_PARAM_MOTOR_DIR,
    NVS_PARAM_IMU_OFFSET,
    NVS_PARAM_LINE_THRESHOLD,
    NVS_PARAM_ENCODER_ZERO,
    NVS_PARAM_BATTERY_CAL,
    NVS_PARAM_LAST_MODE,
    NVS_PARAM_ENCODER_DIR,
    NVS_PARAM_PID_YAW,
    NVS_PARAM_MAG_HEADING,
    NVS_PARAM_PID_DIST,
    NVS_PARAM_LINE_POLARITY,
    NVS_PARAM_LINE_BASE_RPM,
    NVS_PARAM_COUNT
} nvs_param_id_t;

/* -------------------------------------------------------------------------- */
/* 底层 Flash / KV（厂测或调试；业务请用 nvs_param_set_*）                      */
/* -------------------------------------------------------------------------- */

status_t nvs_flash_erase(uint32_t address, uint32_t length, nvs_flash_allow_t allow);
status_t nvs_flash_program(uint32_t address, const void *data, uint32_t length,
                           nvs_flash_allow_t allow);

status_t nvs_init(void);
/** 首次启动种子写入 / SN 补写 / boot_count 落盘（调度器启动后在 app 任务中调用） */
status_t nvs_startup_finalize(void);
uint32_t nvs_active_page_index(void);
#if defined(NVS_CMD_RAW_KV)
void nvs_set_active_page_index(uint32_t page_index);
#endif

status_t nvs_set_u32(const char *ns, const char *key, uint32_t value);
status_t nvs_get_u32(const char *ns, const char *key, uint32_t *value);
#if defined(NVS_CMD_RAW_KV)
status_t nvs_set_blob(const char *ns, const char *key, const void *data, uint32_t len);
status_t nvs_get_blob(const char *ns, const char *key, void *data, uint32_t *len_inout);
#endif

/* -------------------------------------------------------------------------- */
/* 配置缓存与带策略的参数 API                                                    */
/* -------------------------------------------------------------------------- */

const nvs_cfg_t *nvs_cfg_get(void);
bool nvs_first_boot(void);
bool nvs_param_write_allowed(nvs_param_id_t id, nvs_write_src_t src);

status_t nvs_param_set_serial(const char *serial, nvs_write_src_t src);
status_t nvs_param_set_hw_rev(u32_t hw_rev, nvs_write_src_t src);
status_t nvs_param_set_fw_version(const char *version, nvs_write_src_t src);
status_t nvs_param_inc_boot_count(void);
status_t nvs_param_set_pid_speed(const nvs_pid3_t *pid, nvs_write_src_t src);
status_t nvs_param_set_pid_line(const nvs_pid3_t *pid, nvs_write_src_t src);
status_t nvs_param_set_pid_yaw(const nvs_pid3_t *pid, nvs_write_src_t src);
status_t nvs_param_set_pid_dist(const nvs_pid3_t *pid, nvs_write_src_t src);
status_t nvs_param_set_spd_limit(const nvs_spd_limit_t *limit, nvs_write_src_t src);
status_t nvs_param_set_kinematics(const nvs_kinematics_t *kinem, nvs_write_src_t src);
status_t nvs_param_set_motor_dir_mask(u32_t mask, nvs_write_src_t src);
status_t nvs_param_set_encoder_dir_mask(u32_t mask, nvs_write_src_t src);
status_t nvs_param_set_imu_offset(const nvs_imu_offset_t *offset, nvs_write_src_t src);
status_t nvs_param_set_line_threshold(const nvs_line_threshold_t *threshold, nvs_write_src_t src);
status_t nvs_param_set_line_polarity(u8_t black_active_high, nvs_write_src_t src);
status_t nvs_param_set_line_base_rpm(f32_t base_rpm, nvs_write_src_t src);
status_t nvs_param_set_encoder_zero(const nvs_encoder_zero_t *zero, nvs_write_src_t src);
status_t nvs_param_set_battery_cal(const nvs_battery_cal_t *cal, nvs_write_src_t src);
status_t nvs_param_set_mag_heading_offset(f32_t offset_deg, nvs_write_src_t src);
status_t nvs_param_set_last_mode(nvs_run_mode_t mode, nvs_write_src_t src);

/** 恢复 ctrl/cal/user 参数为默认值；保留 serial / hw_rev / fw_ver / boot_cnt */
status_t nvs_factory_reset(void);

/** 读 / 写 Boot 保留区运行槽（见 flash_layout.h） */
status_t nvs_boot_slot_get(uint32_t *slot_out);
status_t nvs_boot_slot_set(uint32_t slot);

#endif /* NVS_H */
