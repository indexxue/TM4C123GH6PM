/**
 * @file    encoder_polarity_board.h
 * @brief   car-4wd 左右轮编码器/电机极性（单轮逻辑 +RPM = 该侧“前进”）
 *
 * 左右轮在同一 car-forward 时 **物理转向相反**，编码器 raw 极性亦不同：
 *   - M1 左：逻辑前进时 raw 计数 **增加** → 不翻转编码器
 *   - M2 右：同 car-forward 时 raw 与 M1 **相反** → 翻转编码器（mask bit1）
 *
 * MOTOR_POL_Mx_CMD_INVERT：TB6612 方向线相对逻辑 RPM 是否取反（motor_dir_mask）。
 */

#ifndef ENCODER_POLARITY_BOARD_H
#define ENCODER_POLARITY_BOARD_H

/* --- 编码器：逻辑 +RPM 时 raw delta 是否为正 --- */
#define ENCODER_POL_M1_FWD_COUNT_POS  1
#define ENCODER_POL_M2_FWD_COUNT_POS  0
#define ENCODER_POL_M3_FWD_COUNT_POS  1
#define ENCODER_POL_M4_FWD_COUNT_POS  1

/* --- 电机输出：逻辑 RPM 是否对 H 桥取反（实板 M1/M2 物理正方向与线序相反） --- */
#define MOTOR_POL_M1_CMD_INVERT       1
#define MOTOR_POL_M2_CMD_INVERT       1
#define MOTOR_POL_M3_CMD_INVERT       0
#define MOTOR_POL_M4_CMD_INVERT       0

#endif /* ENCODER_POLARITY_BOARD_H */
