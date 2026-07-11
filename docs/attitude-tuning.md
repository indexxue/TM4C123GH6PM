# 姿态解算调参指南

> 面向 **car-4wd / car-2wd** 实机调试。算法背景与选型见 [`attitude-fusion.md`](attitude-fusion.md)。  
> 运行时代码：`Common/src/attitude.c`、`Common/inc/attitude.h`。

---

## 1. 数据流概览

当前实现为 **方案 B（倾斜 / 航向解耦）**，在 x-io Fusion 之上叠加小车策略：

```
每帧 (~50 Hz, app_tmr 20 ms):
  原始 IMU/MAG → 轴映射 (FusionRemap)
       ↓
  FusionBias 陀螺去偏
       ↓
  FusionAhrsUpdateNoMagnetometer   ← 6-DOF，mag 不参与四元数
       ↓
  静止时 accel 锁 roll/pitch      ← attitude_lock_tilt_preserve_yaw
       ↓
  FusionCompass 慢融合 yaw        ← 仅平放、非快速转 yaw 时
       ↓
  欧拉角 ±180° 平滑 → int16 遥测 / 蓝牙协议
```

**调参前先理解**：roll/pitch 与 yaw 由不同路径校正，现象不同应改不同参数，避免「一个旋钮拧全局」。

---

## 2. 调参前必做

### 2.1 轴映射（最高优先级）

文件：`Common/src/attitude.c`

| 宏 | 当前默认 | 说明 |
|----|----------|------|
| `ATTITUDE_IMU_REMAP` | `FusionRemapAlignmentPXPYPZ` | MPU6050 轴 → 车体「前/左/上」 |
| `ATTITUDE_MAG_REMAP` | `FusionRemapAlignmentPXPYPZ` | QMC5883P 轴 → 同一约定 |
| `ATTITUDE_EARTH_CONVENTION` | `FusionConventionNwu` | 导航系：北-西-上 |

可选值见 `third_party/Fusion/Fusion/FusionRemap.h`（24 种对齐）。**IMU 与 MAG 需分别确认**，轴不对时会出现「绕单轴转、三角都在变」。

**快速验收**（UART7 日志或厂测 `att` 命令）：

| 动作 | 期望 |
|------|------|
| 车头抬起（pitch） | pitch 变，roll/yaw 基本不变 |
| 左倾（roll） | roll 变 |
| 原地绕竖直轴转 360° | yaw 单调变化，roll/pitch 变化 < 3° |

### 2.2 陀螺零偏

- 上电 **静止 3~5 s**，等待 FusionBias 收敛。
- UART7 出现 `app:att yaw opt ready mag_trust=1 gyro_bias=1` 表示 bias 就绪。
- NVS 中 `imu_offset.gyro[]` 可在厂测阶段写入，上电由 `attitude_load_nvs_gyro_offset()` 加载。

**`gyro_bias=0` 时 yaw 易漂，先解决 bias 再调 mag 融合系数。**

### 2.3 磁力计标定

金属底盘、电机、电池会造成硬/软铁干扰。当前仅有 **模长 EMA + 跳变门控**，未做椭球标定。**yaw 精度上限很大程度取决于 mag 标定**（见 [`attitude-fusion.md` §6.3](attitude-fusion.md)）。

---

## 3. 参数一览

所有宏定义于 `Common/src/attitude.c`，除非另注。

### 3.1 Fusion 6-DOF（运动态 roll/pitch）

| 宏 | 默认值 | 作用 |
|----|--------|------|
| `ATTITUDE_AHRS_GAIN` | `0.5` | Fusion 对加速度计的信任度；越大运动跟手、越小越抗线加速度 |
| `ATTITUDE_ACCEL_REJECTION_DEG` | `10.0` | 加速度拒绝阈值（°）；合加速度不像重力时忽略 accel |
| `ATTITUDE_RECOVERY_SECONDS` | `5.0` | 拒绝 accel 后，多少秒触发 recovery 重新融合 |
| `ATTITUDE_GYRO_RANGE_DPS` | `250.0` | 与 MPU6050 ±250 °/s 量程一致 |

### 3.2 陀螺零偏估计（FusionBias）

| 宏 | 默认值 | 作用 |
|----|--------|------|
| `ATTITUDE_BIAS_STATIONARY_DPS` | `2.0` | 角速度低于此值视为「静止」，才更新 bias |
| `ATTITUDE_BIAS_STATIONARY_SEC` | `3.0` | 持续静止多久后开始估计 bias |

### 3.3 静止 tilt 锁定（accel 校正 roll/pitch）

| 宏 | 默认值 | 作用 |
|----|--------|------|
| `ATTITUDE_ACCEL_NORM_TOL_G` | `0.15` | `\|a\|` 与 1 g 偏差在此内视为「近似静止」 |
| `ATTITUDE_TILT_LOCK_MAX_DPS` | `15.0` | 角速度峰值低于此值且 `\|a\|≈1g` 时，用 accel **硬锁** roll/pitch |

### 3.4 罗盘 yaw 融合

| 宏 | 默认值 | 作用 |
|----|--------|------|
| `ATTITUDE_MAG_YAW_ALPHA` | `0.12` | mag 航向与当前 yaw 的融合系数（一阶低通） |
| `ATTITUDE_MAG_YAW_SKIP_GZ_DPS` | `8.0` | `\|gz\|` 超过此值视为有意绕 yaw 转，**暂停** mag 校正 |
| `ATTITUDE_MAG_YAW_SKIP_XY_DPS` | `5.0` | `\|gx\|` 或 `\|gy\|` 超过此值视为 roll/pitch 转，暂停 mag |
| `ATTITUDE_MAG_DECIM` | `2`（`attitude.h`） | 每 N 次 IMU 更新读一次 mag；2 → 50 Hz 下约 25 Hz |

> **注意**：`ATTITUDE_TILT_LOCK_MAX_DPS` 同时参与 **tilt lock** 与 **mag yaw 融合** 的门控（`attitude_can_blend_mag_yaw`）。

### 3.5 磁力计可信度

| 宏 | 默认值 | 作用 |
|----|--------|------|
| `ATTITUDE_MAG_NORM_JUMP` | `0.22` | mag 模长相对 EMA 跳变超过此比例 → `mag_trust=0` |
| `ATTITUDE_MAG_NORM_ALPHA` | `0.05` | mag 模长 EMA 平滑系数 |

---

## 4. 按现象调参

### 4.1 Roll / Pitch

| 现象 | 建议调整 | 方向 |
|------|----------|------|
| 急加速 / 急刹时 tilt 被拉歪 | `ATTITUDE_ACCEL_REJECTION_DEG` | **减小**（如 10 → 8 → 6） |
| 运动干扰过后 tilt 恢复太慢 | `ATTITUDE_RECOVERY_SECONDS` | **减小**（如 5 → 3 s） |
| 运动时 tilt 跟手但停稳后慢慢漂 | `ATTITUDE_AHRS_GAIN` | **略增**（0.5 → 0.6~0.8） |
| 停稳后 roll/pitch 仍慢慢漂 | `ATTITUDE_TILT_LOCK_MAX_DPS` | **略增**（15 → 18~20） |
| 停稳后 tilt 抖、来回弹 | `ATTITUDE_TILT_LOCK_MAX_DPS` | **减小**（15 → 12）；`ATTITUDE_ACCEL_NORM_TOL_G` **收紧**（0.15 → 0.12） |
| 小坡 / 轻微振动时锁不住 tilt | `ATTITUDE_ACCEL_NORM_TOL_G` | **略放宽**（0.15 → 0.18） |
| 长时间静止 gyro 零偏仍大 | `ATTITUDE_BIAS_STATIONARY_DPS` | **减小**（2 → 1.5） |
| bias 收敛太慢 | `ATTITUDE_BIAS_STATIONARY_SEC` | **减小**（3 → 2 s） |

**急停 tilt 被拉飞 — 起步组合示例：**

```c
#define ATTITUDE_ACCEL_REJECTION_DEG    8.0f    /* 原 10 */
#define ATTITUDE_TILT_LOCK_MAX_DPS      12.0f   /* 原 15，减少运动中的硬锁 */
```

### 4.2 Yaw

| 现象 | 建议调整 | 方向 |
|------|----------|------|
| 静止 yaw 慢慢漂（如 ~2°/s） | `ATTITUDE_MAG_YAW_ALPHA` | **增大**（0.12 → 0.15~0.20） |
| 静止 yaw 抖、磁干扰时跳 | `ATTITUDE_MAG_YAW_ALPHA` | **减小**（0.12 → 0.08~0.10） |
| 转 yaw 时 mag 把方向拉回去 | `ATTITUDE_MAG_YAW_SKIP_GZ_DPS` | **减小**（8 → 5~6） |
| 慢速转圈 yaw 与手感不一致 | `ATTITUDE_MAG_YAW_SKIP_GZ_DPS` | **略增**（8 → 10~12） |
| 转 roll/pitch 时 yaw 被带偏 | `ATTITUDE_MAG_YAW_SKIP_XY_DPS` | **减小**（5 → 3~4） |
| 电机转 / 金属靠近 yaw 乱跳 | `ATTITUDE_MAG_NORM_JUMP` | **减小**（0.22 → 0.15~0.18） |
| mag 恢复信任太慢 | `ATTITUDE_MAG_NORM_ALPHA` | **略增**（0.05 → 0.08） |
| yaw 校正响应太慢 | `ATTITUDE_MAG_DECIM` | 改为 `1`（更勤读 mag） |
| I2C 负载过高 | `ATTITUDE_MAG_DECIM` | 改为 `3` 或 `4` |

**静止稳、转圈跟手 — 起步组合示例：**

```c
#define ATTITUDE_MAG_YAW_ALPHA          0.12f   /* 当前默认，静止已 OK 可不动 */
#define ATTITUDE_MAG_YAW_SKIP_GZ_DPS    6.0f    /* 原 8，更早停 mag */
#define ATTITUDE_MAG_YAW_SKIP_XY_DPS    4.0f    /* 原 5 */
```

### 4.3 参数耦合提醒

| 共用条件 | 涉及逻辑 |
|----------|----------|
| `\|a\|≈1g` | tilt lock、mag yaw 融合 |
| `gyro_peak < ATTITUDE_TILT_LOCK_MAX_DPS` | tilt lock、mag yaw 融合 |
| `\|gz\| < SKIP_GZ` 且 `\|gx\|,\|gy\| < SKIP_XY` | 仅 mag yaw 融合 |

增大 `ATTITUDE_TILT_LOCK_MAX_DPS` 会 **同时** 让运动中也更容易锁 tilt 和融 mag；减小则两者都更保守。

---

## 5. 推荐调参流程

### 5.1 准备

1. 接 **UART7**（PE1 TX @ 115200），查看 `app:att` 周期日志。
2. 确认 `gyro_bias=1` 后再调 yaw 相关参数。
3. 修改 `Common/src/attitude.c` 后执行 `.\build.cmd`，烧录 `projects/car-4wd/build/car-4wd.bin`（或 factory 做厂测）。

### 5.2 步骤

1. **验收轴映射**（§2.1 三动作测试）。
2. **上电静止 5 s**，确认 bias 收敛。
3. 按 §6 标准用例逐项测试，记录 **改前 / 改后** 日志。
4. **每次只改 1~2 个参数**，步长约 20~30%，避免多变量叠加无法归因。
5. 优先级：**轴映射 → gyro bias → mag 标定 → yaw 融合系数 → Fusion gain/rejection**。

### 5.3 日志与状态

| 来源 | 内容 |
|------|------|
| `app:att roll=… pitch=… yaw=… deg` | 1 s 周期姿态（`APP_ATT_LOG_PERIOD_MS=1000`） |
| `app:att yaw opt ready mag_trust=… gyro_bias=…` | bias 就绪与 mag 信任（仅打印一次） |
| 厂测 UART7 `att` | 即时读欧拉角 |
| `attitude_get_status()` | `mag_trust`、`gyro_bias_ready`（协议/调试可扩展） |

`mag_trust=0` 表示 mag 被模长跳变门控，此时 yaw 仅靠陀螺积分，漂移属预期行为。

---

## 6. 标准验收用例

与 [`attitude-fusion.md` §6.4](attitude-fusion.md) 一致，调参前后均应复测：

| # | 动作 | 通过标准（建议） |
|---|------|------------------|
| 1 | 平放静止 30 s | \|yaw 漂移\| < 2°/min（mag 可信时） |
| 2 | 平放绕竖直轴匀速转 360° | yaw 单调；roll/pitch 变化 < 3° |
| 3 | 急加速 / 急停（不转 yaw） | roll/pitch 瞬态 < 5°，1 s 内回稳 |
| 4 | 连续转过 ±180° | 显示无 300°+ 视觉跳变 |
| 5 | 电机全速空转（磁干扰） | yaw 无持续单边漂移；`mag_trust` 可短暂为 0 |

---

## 7. 进一步改善（超出宏调参）

| 手段 | 收益 | 参考 |
|------|------|------|
| 厂测 mag 8 字 / hard iron 标定 → NVS | yaw 精度、抗磁干扰 | `attitude-fusion.md` §6.3 |
| 编码器轮速差分 yaw 融合 | 运动态航向比 mag 稳 | `attitude-fusion.md` §5.2 |
| MPU6050 六面 accel 标定 | 大倾角 tilt 精度 | 厂测扩展 |
| 确认量程 ±2 g / ±250 °/s | 急甩动是否饱和 | `cbb/mpu6050` 配置 |

---

## 8. 相关文件

| 路径 | 说明 |
|------|------|
| `Common/src/attitude.c` | 全部可调宏与融合策略 |
| `Common/inc/attitude.h` | API、`ATTITUDE_MAG_DECIM` |
| `third_party/Fusion/Fusion/` | FusionAhrs、FusionBias、FusionCompass |
| `docs/attitude-fusion.md` | 方案调研、选型与标定框架 |
| `docs/bluetooth-protocol.md` | 遥测 roll/pitch/yaw（i16，-180~180°） |
| `projects/car-4wd/main/app.c` | 50 Hz 采样、`app:att` 日志 |
| `factory/factory.c` | 厂测 `att` 命令 |

---

## 9. 修订记录

| 日期 | 说明 |
|------|------|
| 2026-07 | 初版：Fusion 6-DOF + tilt lock + compass yaw 解耦策略调参说明 |
