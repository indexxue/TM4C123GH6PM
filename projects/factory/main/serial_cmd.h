/**
 * @file    serial_cmd.h
 * @brief   厂测 UART7 外设/芯片测试命令（注册到 Common cmd 框架）
 */

#ifndef MODULE_SERIAL_CMD_H
#define MODULE_SERIAL_CMD_H

#ifdef __cplusplus
extern "C" {
#endif

/** 注册厂测专用命令：sn / led / buzzer / bat / enc / imu / mag / ultra / att / ftm */
void serial_cmd_register_defaults(void);

#ifdef __cplusplus
}
#endif

#endif /* MODULE_SERIAL_CMD_H */
