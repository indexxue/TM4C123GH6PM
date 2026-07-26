/**
 * @file    serial_cmd.c
 * @brief   厂测外设测试命令（按 device_profile / BOARD_*_COUNT 注册）
 *
 * 公共命令仍由 Common/src/cmd.c 提供；本文件只注册厂测扩展。
 */

#include "serial_cmd.h"

#include "attitude.h"
#include "battery.h"
#include "board.h"
#include "buzzer.h"
#include "camera_spi.h"
#include "cmd.h"
#include "device_profile.h"
#include "flash_layout.h"
#include "imu.h"
#include "led_scene.h"
#include "magnetometer.h"
#include "nvs.h"
#include "ultrasonic.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FACTORY_FW_VERSION "ft-0.2.0"

#ifndef BOARD_MOTOR_COUNT
#define BOARD_MOTOR_COUNT 0U
#endif
#ifndef BOARD_ENCODER_COUNT
#define BOARD_ENCODER_COUNT 0U
#endif

/* -------------------------------------------------------------------------- */
/* sn / devtype / ftm                                                         */
/* -------------------------------------------------------------------------- */

static void cmd_sn(int argc, const char *argv[])
{
    const nvs_cfg_t *cfg;

    if (argc == 1) {
        cfg = nvs_cfg_get();
        cmd_reply_ok("sn", (cfg->serial[0] != '\0') ? cfg->serial : "-");
        return;
    }

    if (argc == 2) {
        size_t len = strlen(argv[1]);

        if ((len >= 8U) && (len < NVS_CFG_SERIAL_MAX) &&
            (nvs_param_set_serial(argv[1], NVS_WRITE_SRC_FACTORY) == STATUS_OK)) {
            cmd_reply_ok("sn", "set");
            return;
        }
    }

    cmd_reply_ng();
}

static void cmd_devtype(int argc, const char *argv[])
{
    char buf[48];
    const device_product_profile_t *profile = device_profile_product();

    (void)argc;
    (void)argv;

    (void)snprintf(buf, sizeof(buf), "%lu %s motors=%u",
                   (unsigned long)profile->product_id,
                   profile->name,
                   (unsigned)BOARD_MOTOR_COUNT);
    cmd_reply_ok("devtype", buf);
}

static void cmd_ftm(int argc, const char *argv[])
{
    char buf[CMD_STATUS_BUF_SIZE];
    const device_product_profile_t *profile = device_profile_product();

    if ((argc >= 2) && (strcmp(argv[1], "version") == 0)) {
        cmd_reply_ok("ftm", FACTORY_FW_VERSION);
        return;
    }

    (void)snprintf(buf, sizeof(buf), "version=%s product=%s base=0x%08lX",
                   FACTORY_FW_VERSION, profile->name, (unsigned long)FLASH_APP_B_BASE);
    cmd_reply_ok("ftm", buf);
}

/* -------------------------------------------------------------------------- */
/* led / buzzer / bat                                                         */
/* -------------------------------------------------------------------------- */

static void cmd_led(int argc, const char *argv[])
{
    if (argc < 2) {
        cmd_reply_ng();
        return;
    }

    if ((strcmp(argv[1], "on") == 0) ||
        ((argc >= 3) && (strcmp(argv[1], "all") == 0) && (strcmp(argv[2], "on") == 0))) {
        led_scene_led_direct_set(LED_SCENE_LED_0, true);
        cmd_reply_ok("led", "on");
        return;
    }

    if ((strcmp(argv[1], "off") == 0) ||
        ((argc >= 3) && (strcmp(argv[1], "all") == 0) && (strcmp(argv[2], "off") == 0))) {
        led_scene_led_direct_set(LED_SCENE_LED_0, false);
        cmd_reply_ok("led", "off");
        return;
    }

    if (strcmp(argv[1], "r") == 0) {
        led_scene_rgb_set(64U, 0U, 0U);
        cmd_reply_ok("led", "r");
        return;
    }
    if (strcmp(argv[1], "g") == 0) {
        led_scene_rgb_set(0U, 64U, 0U);
        cmd_reply_ok("led", "g");
        return;
    }
    if (strcmp(argv[1], "b") == 0) {
        led_scene_rgb_set(0U, 0U, 64U);
        cmd_reply_ok("led", "b");
        return;
    }

    if ((strcmp(argv[1], "rgb") == 0) && (argc >= 5)) {
        uint8_t r = (uint8_t)strtoul(argv[2], NULL, 0);
        uint8_t g = (uint8_t)strtoul(argv[3], NULL, 0);
        uint8_t b = (uint8_t)strtoul(argv[4], NULL, 0);

        led_scene_rgb_set(r, g, b);
        cmd_reply_ok("led", "rgb");
        return;
    }

    cmd_reply_ng();
}

static void cmd_buzzer(int argc, const char *argv[])
{
    if (argc < 2) {
        cmd_reply_ng();
        return;
    }

    if (strcmp(argv[1], "off") == 0) {
        buzzer_stop();
        cmd_reply_ok("buzzer", "off");
        return;
    }

    if (strcmp(argv[1], "on") == 0) {
        uint32_t freq = BUZZER_DEFAULT_FREQ_HZ;

        if (argc >= 3) {
            freq = (uint32_t)strtoul(argv[2], NULL, 10);
        }
        buzzer_start(freq, BUZZER_DEFAULT_DUTY_PERCENT);
        cmd_reply_ok("buzzer", "on");
        return;
    }

    if (strcmp(argv[1], "beep") == 0) {
        uint32_t freq = BUZZER_DEFAULT_FREQ_HZ;
        uint32_t ms = 200U;

        if (argc >= 3) {
            freq = (uint32_t)strtoul(argv[2], NULL, 10);
        }
        if (argc >= 4) {
            ms = (uint32_t)strtoul(argv[3], NULL, 10);
        }
        if (!buzzer_beep(freq, ms)) {
            cmd_reply_ng();
            return;
        }
        cmd_reply_ok("buzzer", "beep");
        return;
    }

    cmd_reply_ng();
}

static void cmd_bat(int argc, const char *argv[])
{
    battery_voltage_t batt = {0};
    battery_info_t info = {0};
    char buf[CMD_STATUS_BUF_SIZE];

    (void)argc;
    (void)argv;

    if (!battery_percent_update()) {
        cmd_reply_ng();
        return;
    }
    if (!battery_info_read(&info, &batt)) {
        cmd_reply_ng();
        return;
    }

    (void)snprintf(buf, sizeof(buf), "%lu.%03luV %u%% lv=%u",
                   (unsigned long)(batt.current_mv / 1000U),
                   (unsigned long)(batt.current_mv % 1000U),
                   (unsigned)info.percent,
                   (unsigned)info.level);
    cmd_reply_ok("bat", buf);
}

/* -------------------------------------------------------------------------- */
/* enc / imu / mag / ultra / att                                              */
/* -------------------------------------------------------------------------- */

static void cmd_enc(int argc, const char *argv[])
{
    char buf[CMD_STATUS_BUF_SIZE];
    uint8_t i;
    int n = 0;

    if (BOARD_ENCODER_COUNT == 0U) {
        cmd_reply_ok("enc", "ng:none");
        return;
    }

    if ((argc >= 2) && (strcmp(argv[1], "reset") == 0)) {
        for (i = 0U; i < BOARD_ENCODER_COUNT; i++) {
            Encoder_ResetCount(i);
        }
        cmd_reply_ok("enc", "reset");
        return;
    }

    for (i = 0U; i < BOARD_ENCODER_COUNT; i++) {
        int w = snprintf(buf + n, sizeof(buf) - (size_t)n, "%sm%u=%ld",
                         (i == 0U) ? "" : " ",
                         (unsigned)(i + 1U),
                         (long)Encoder_GetCount(i));
        if (w < 0) {
            break;
        }
        n += w;
        if ((size_t)n >= sizeof(buf)) {
            break;
        }
    }
    cmd_reply_ok("enc", buf);
}

static void cmd_imu(int argc, const char *argv[])
{
    imu_sample_t imu;
    int16_t temp_c = 0;
    char buf[CMD_STATUS_BUF_SIZE];

    (void)argc;
    (void)argv;

    if (!imu_is_ready()) {
        if (imu_init() != STATUS_OK) {
            cmd_reply_ok("imu", "ng:init");
            return;
        }
    }

    if (imu_read_sample(&imu) != STATUS_OK) {
        cmd_reply_ok("imu", "ng:read");
        return;
    }

    (void)imu_read_temperature(&temp_c);
    (void)snprintf(buf, sizeof(buf),
                   "ax=%d ay=%d az=%d gx=%d gy=%d gz=%d t=%dC",
                   (int)imu.ax, (int)imu.ay, (int)imu.az,
                   (int)imu.gx, (int)imu.gy, (int)imu.gz,
                   (int)temp_c);
    cmd_reply_ok("imu", buf);
}

static void cmd_mag(int argc, const char *argv[])
{
    magnetometer_sample_t mag;
    char buf[CMD_STATUS_BUF_SIZE];

    (void)argc;
    (void)argv;

    if (!magnetometer_is_ready()) {
        if (magnetometer_init() != STATUS_OK) {
            cmd_reply_ok("mag", "ng:init");
            return;
        }
    }

    if (magnetometer_read_sample(&mag) != STATUS_OK) {
        cmd_reply_ok("mag", "ng:read");
        return;
    }

    (void)snprintf(buf, sizeof(buf), "mx=%d my=%d mz=%d",
                   (int)mag.mx, (int)mag.my, (int)mag.mz);
    cmd_reply_ok("mag", buf);
}

static void cmd_ultra(int argc, const char *argv[])
{
    uint16_t mm = 0U;
    char buf[32];

    (void)argc;
    (void)argv;

    if (!ultrasonic_is_ready()) {
        if (ultrasonic_init() != STATUS_OK) {
            cmd_reply_ok("ultra", "ng:init");
            return;
        }
    }

    if (ultrasonic_measure_mm(&mm) != STATUS_OK) {
        cmd_reply_ok("ultra", "ng:timeout");
        return;
    }

    (void)snprintf(buf, sizeof(buf), "%umm", (unsigned)mm);
    cmd_reply_ok("ultra", buf);
}

static void cmd_att(int argc, const char *argv[])
{
    imu_sample_t imu;
    magnetometer_sample_t mag;
    attitude_euler_t euler;
    char buf[CMD_STATUS_BUF_SIZE];

    (void)argc;
    (void)argv;

    if (!attitude_is_ready()) {
        cmd_reply_ok("att", "ng:not ready");
        return;
    }
    if ((imu_read_sample(&imu) != STATUS_OK) || (magnetometer_read_sample(&mag) != STATUS_OK)) {
        cmd_reply_ok("att", "ng:sensor read");
        return;
    }
    if (attitude_update_from_sensors(&imu, &mag) != STATUS_OK) {
        cmd_reply_ok("att", "ng:fusion");
        return;
    }
    if (attitude_get_euler(&euler) != STATUS_OK) {
        cmd_reply_ok("att", "ng:euler");
        return;
    }

    (void)snprintf(buf, sizeof(buf), "roll=%d pitch=%d yaw=%d deg",
                   (int)euler.roll, (int)euler.pitch, (int)euler.yaw);
    cmd_reply_ok("att", buf);
}

/* -------------------------------------------------------------------------- */
/* cam — Camera SPI L2/L3 联调                                              */
/* -------------------------------------------------------------------------- */

static const char *cam_link_str(camera_spi_link_t link)
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

static const char *cam_ctrl_str(camera_spi_ctrl_state_t st)
{
    switch (st) {
    case CAMERA_SPI_CTRL_PENDING:
        return "PENDING";
    case CAMERA_SPI_CTRL_DONE:
        return "DONE";
    case CAMERA_SPI_CTRL_TIMEOUT:
        return "TIMEOUT";
    case CAMERA_SPI_CTRL_IDLE:
    default:
        return "IDLE";
    }
}

static void cmd_cam(int argc, const char *argv[])
{
    char buf[128];
    status_t st;
    camera_spi_stats_t stats;
    camera_spi_detect_t det;
    camera_spi_servo_t servo;
    camera_spi_ctrl_status_t ctrl;

    if (camera_spi_is_ready() == FALSE) {
        cmd_reply_ok("cam", "ng:not_ready");
        return;
    }

    if ((argc < 2) || (strcmp(argv[1], "status") == 0)) {
        if (camera_spi_get_stats(&stats) != STATUS_OK) {
            cmd_reply_ng();
            return;
        }
        (void)camera_spi_get_ctrl_status(&ctrl);
        (void)snprintf(buf, sizeof(buf),
                       "link=%s peer=%u ok=%lu mag=%lu crc=%lu det=%u servo=%u "
                       "ack_ok=%u ack_fail=%u to=%u ctrl=%s",
                       cam_link_str(stats.link),
                       (unsigned)stats.peer_role,
                       (unsigned long)stats.rx_ok,
                       (unsigned long)stats.magic_err,
                       (unsigned long)stats.crc_err,
                       (unsigned)stats.detect_rx,
                       (unsigned)stats.servo_rx,
                       (unsigned)stats.ctrl_ack_ok,
                       (unsigned)stats.ctrl_ack_fail,
                       (unsigned)stats.ctrl_timeout,
                       cam_ctrl_str(ctrl.state));
        cmd_reply_ok("cam", buf);
        return;
    }

    if (strcmp(argv[1], "detect") == 0) {
        if (camera_spi_get_detect(&det) != STATUS_OK) {
            cmd_reply_ng();
            return;
        }
        if (det.valid == FALSE) {
            cmd_reply_ok("cam", "detect=none");
            return;
        }
        (void)snprintf(buf, sizeof(buf),
                       "n=%u best=%u %ux%u box0=(%u,%u,w=%u sc=%u)",
                       (unsigned)det.count, (unsigned)det.best_index,
                       (unsigned)det.frame_w, (unsigned)det.frame_h,
                       (unsigned)det.box[0].x, (unsigned)det.box[0].y,
                       (unsigned)det.box[0].w, (unsigned)det.box[0].score_u8);
        cmd_reply_ok("cam", buf);
        return;
    }

    if (strcmp(argv[1], "servo") == 0) {
        if (camera_spi_get_servo(&servo) != STATUS_OK) {
            cmd_reply_ng();
            return;
        }
        if (servo.valid == FALSE) {
            cmd_reply_ok("cam", "servo=none");
            return;
        }
        (void)snprintf(buf, sizeof(buf), "pan=%d tilt=%d us=%u/%u",
                       (int)servo.pan_deg_x100, (int)servo.tilt_deg_x100,
                       (unsigned)servo.pan_pulse_us, (unsigned)servo.tilt_pulse_us);
        cmd_reply_ok("cam", buf);
        return;
    }

    if (strcmp(argv[1], "center") == 0) {
        st = camera_spi_ctrl_servo_center();
        if (st != STATUS_OK) {
            cmd_reply_ok("cam", "ng:ctrl");
            return;
        }
        cmd_reply_ok("cam", "center queued");
        return;
    }

    if (strcmp(argv[1], "detect_on") == 0) {
        st = camera_spi_ctrl_detect_enable(1U);
        if (st != STATUS_OK) {
            cmd_reply_ok("cam", "ng:ctrl");
            return;
        }
        cmd_reply_ok("cam", "detect_on queued");
        return;
    }

    if (strcmp(argv[1], "detect_off") == 0) {
        st = camera_spi_ctrl_detect_enable(0U);
        if (st != STATUS_OK) {
            cmd_reply_ok("cam", "ng:ctrl");
            return;
        }
        cmd_reply_ok("cam", "detect_off queued");
        return;
    }

    if ((strcmp(argv[1], "angle") == 0) && (argc >= 4)) {
        st = camera_spi_ctrl_servo_set_angle((uint8_t)atoi(argv[2]),
                                            (int16_t)atoi(argv[3]));
        if (st != STATUS_OK) {
            cmd_reply_ok("cam", "ng:ctrl");
            return;
        }
        cmd_reply_ok("cam", "angle queued");
        return;
    }

    if ((strcmp(argv[1], "nudge") == 0) && (argc >= 4)) {
        st = camera_spi_ctrl_servo_nudge((uint8_t)atoi(argv[2]),
                                        (int16_t)atoi(argv[3]));
        if (st != STATUS_OK) {
            cmd_reply_ok("cam", "ng:ctrl");
            return;
        }
        cmd_reply_ok("cam", "nudge queued");
        return;
    }

    if (strcmp(argv[1], "net") == 0) {
        camera_spi_net_info_t net;
        uint32_t ip;

        if (camera_spi_get_net_info(&net) != STATUS_OK) {
            cmd_reply_ng();
            return;
        }
        if ((net.valid == FALSE) || ((net.flags & CAMERA_SPI_NET_FLAG_HAS_IP) == 0U)) {
            (void)camera_spi_request_net_info();
            (void)camera_spi_get_net_info(&net);
        }
        if (net.valid == FALSE) {
            cmd_reply_ok("cam", "net=none");
            return;
        }
        ip = net.ipv4;
        (void)snprintf(buf, sizeof(buf),
                       "ip=%u.%u.%u.%u port=%u mode=%u flags=0x%02X path=%u",
                       (unsigned)(ip & 0xFFU),
                       (unsigned)((ip >> 8) & 0xFFU),
                       (unsigned)((ip >> 16) & 0xFFU),
                       (unsigned)((ip >> 24) & 0xFFU),
                       (unsigned)net.http_port,
                       (unsigned)net.wifi_mode,
                       (unsigned)net.flags,
                       (unsigned)net.stream_path_id);
        cmd_reply_ok("cam", buf);
        return;
    }

    cmd_reply_ng();
}

/* -------------------------------------------------------------------------- */

void serial_cmd_register_defaults(void)
{
    (void)cmd_register("sn", cmd_sn, "sn [serial] get/set NVS serial");
    (void)cmd_register("devtype", cmd_devtype, "devtype product id + motor count");
    (void)cmd_register("ftm", cmd_ftm, "ftm [version] factory status");

    if (device_profile_board_wants(DEVICE_BOARD_MASK_LED)) {
        (void)cmd_register("led", cmd_led, "led on|off|r|g|b|rgb <r> <g> <b>");
    }
    if (device_profile_board_wants(DEVICE_BOARD_MASK_BUZZER)) {
        (void)cmd_register("buzzer", cmd_buzzer, "buzzer on|off|beep [freq] [ms]");
    }
    if (device_profile_board_wants(DEVICE_BOARD_MASK_BATTERY)) {
        (void)cmd_register("bat", cmd_bat, "bat voltage percent");
    }
    if (device_profile_board_wants(DEVICE_BOARD_MASK_ENCODER) && (BOARD_ENCODER_COUNT > 0U)) {
        (void)cmd_register("enc", cmd_enc, "enc [reset] encoder counts");
    }
    if (device_profile_board_wants(DEVICE_BOARD_MASK_IMU)) {
        (void)cmd_register("imu", cmd_imu, "imu read MPU6050 sample");
    }
    if (device_profile_board_wants(DEVICE_BOARD_MASK_MAG)) {
        (void)cmd_register("mag", cmd_mag, "mag read QMC5883P sample");
    }
    if (device_profile_board_wants(DEVICE_BOARD_MASK_ULTRA)) {
        (void)cmd_register("ultra", cmd_ultra, "ultra HC-SR04 distance mm");
    }
    if (device_profile_board_wants(DEVICE_BOARD_MASK_SENSORS)) {
        (void)cmd_register("att", cmd_att, "att euler angles deg");
    }
    if (device_profile_board_wants(DEVICE_BOARD_MASK_PERIPH)) {
        (void)cmd_register("cam", cmd_cam,
                           "cam [status|detect|servo|center|detect_on|detect_off|angle|nudge|net]");
    }
}
