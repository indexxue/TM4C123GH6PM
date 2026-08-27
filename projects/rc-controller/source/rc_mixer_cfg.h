/**
 * @file    rc_mixer_cfg.h
 * @brief   Tilt 混控编译期配置（改完重编，勿手改 rc_mixer.c 里的符号）
 *
 * 本板标定（Tilt Probe）结论：
 * - 手柄 **左右倾 (roll)** → 车 **前/后 (throttle)**
 * - 手柄 **前/后倾 (pitch)** → 车 **左/右转 (steer)**
 *
 * 排错指南（只改一个宏，重编烧录）：
 * | 现象              | 改这个宏              |
 * |-------------------|-----------------------|
 * | 前/后倾转向反了   | RC_MIXER_TILT_INVERT_PITCH |
 * | 左/右倾前进后退反 | RC_MIXER_TILT_INVERT_ROLL  |
 * | 两轴完全对调      | RC_MIXER_TILT_SWAP_AXES    |
 */

#ifndef RC_MIXER_CFG_H
#define RC_MIXER_CFG_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 0：roll→throttle(Y)，pitch→steer(X)（本板默认）
 * 1：roll→steer(X)，pitch→throttle(Y)
 */
#ifndef RC_MIXER_TILT_SWAP_AXES
#define RC_MIXER_TILT_SWAP_AXES         0
#endif

/** 1：rel_roll 取反后再参与映射（右倾本应为前进） */
#ifndef RC_MIXER_TILT_INVERT_ROLL
#define RC_MIXER_TILT_INVERT_ROLL         1
#endif

/** 1：rel_pitch 取反后再参与映射（前/后倾本应对应转向） */
#ifndef RC_MIXER_TILT_INVERT_PITCH
#define RC_MIXER_TILT_INVERT_PITCH        0
#endif

/**
 * Tilt cmd 低通：每 20ms 向目标靠拢 1/2^N；0=关闭。
 * N=2 → 约 80ms 时间常数；N=3 → 更柔。
 */
#ifndef RC_MIXER_TILT_SMOOTH_SHIFT
#define RC_MIXER_TILT_SMOOTH_SHIFT        2
#endif

#ifdef __cplusplus
}
#endif

#endif /* RC_MIXER_CFG_H */
