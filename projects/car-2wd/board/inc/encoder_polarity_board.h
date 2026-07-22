/**
 * @file    encoder_polarity_board.h
 * @brief   car-2wd 左右轮编码器/电机极性（M1 左 + M2 右 + 万向轮；地面差速）
 *
 * 左右轮在同一 car-forward 时物理转向相反，编码器 raw 极性亦不同：
 *   - M1 左：逻辑前进时 raw 计数增加 → 不翻转编码器
 *   - M2 右：同 car-forward 时 raw 与 M1 相反 → 翻转编码器（mask bit1）
 *
 * 初值对齐 car-4wd 的 M1/M2；实机若方向反了用 PARAM / CMD 改 mask。
 */

#ifndef ENCODER_POLARITY_BOARD_H
#define ENCODER_POLARITY_BOARD_H

/* --- 编码器：逻辑 +RPM 时 raw delta 是否为正 --- */
#define ENCODER_POL_M1_FWD_COUNT_POS  1
#define ENCODER_POL_M2_FWD_COUNT_POS  0

/* --- 电机输出：逻辑 RPM 是否对 H 桥取反 --- */
#define MOTOR_POL_M1_CMD_INVERT       1
#define MOTOR_POL_M2_CMD_INVERT       1

#endif /* ENCODER_POLARITY_BOARD_H */
