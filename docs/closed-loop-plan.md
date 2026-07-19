# 小车闭环控制实现计划

> 面向 **car-4wd**（四轮地面，主目标）与 **car-2wd**（含后续 **两轮自平衡** 形态）。  
> 本文定义闭环分层、控制环选型、模块划分、**先后实现顺序**与每阶段验收标准。  
> 构建与烧录见 [`build.md`](build.md)；姿态解算见 [`attitude-tuning.md`](attitude-tuning.md)。

---

## 1. 目标与范围

### 1.1 闭环目标

| 层级 | 控制对象 | 传感器 | 输出 |
|------|----------|--------|------|
| **内环** | 四轮（或两轮）轮速 | 编码器 | 各电机 PWM / 等效 RPM |
| **外环 — 遥控** | 车体线速度 + 转向 | 无（开环期望） | 左右轮速设定 |
| **外环 — 循迹** | 横向偏差（相对黑线） | 6 路 TCRT5000 ADC | 转向修正量叠加到基准速度 |
| **可选** | 航向角 | IMU yaw / 编码器差速 | 转向修正（后续迭代） |

最终用户可见能力：

1. **遥控模式**：蓝牙 throttle/steer → 限速/限加速度 → 四轮等速差速 → **轮速 PID 跟踪**。
2. **循迹模式**：沿黑线自主行驶，丢线停车或搜索。
3. **参数可调**：`pid_spd`、`pid_line`、运动学、阈值等经 UART7 `cmd` 或蓝牙协议写入 NVS。

### 1.2 不在 car-4wd 首版闭环范围内

- 路径规划、SLAM、多段赛道策略
- 超声波避障闭环（仅预留接口）
- 编码器融合 yaw 替代磁力计（见 [`attitude-tuning.md`](attitude-tuning.md) §7，属姿态增强）
- **直立环**（见 §1.3）— 四轮静态稳定，**car-4wd 不需要**；**car-2wd 自平衡**在四轮闭环共用模块就绪后再做（§5.9）

### 1.3 控制环选型：速度 / 角度 / 距离 / 直立

竞赛与平衡车语境里常提到四类环。下表说明 **对本仓库两种车型是否必要**，避免 car-4wd 阶段误做直立环，也避免 car-2wd 自平衡时漏做。

| 控制环 | 控制量 | 典型传感器 | car-4wd（四轮地面） | car-2wd 自平衡（后续） |
|--------|--------|------------|---------------------|------------------------|
| **速度环** | 轮速 RPM | 编码器 | **必做**（内环，§5 阶段 0~3） | **必做**（内环或并入直立输出） |
| **角度环** | 航向 yaw | IMU / 编码器里程计 | **可选**（直行保向、定角度转向，§5.8） | **需要**（转向：遥控 steer → yaw 差 / 差速） |
| **距离环** | 位移 / 路程 | 编码器积分、超声波 | **可选**（定距停车、避障，§5.8） | **可选**（定距、位置保持） |
| **直立环** | 倾角 pitch/roll | IMU 加速度+陀螺 | **不需要**（四轮着地，静态稳定） | **必做**（倒立摆，防倾覆） |

#### car-4wd：首版只需「速度环 + 一个外环」

```text
  遥控：throttle/steer → 左右轮目标 RPM（差速，开环期望）
  循迹：6 路 ADC → 横向偏差 → pid_line → turn 修正
                    ↓
            四轮速度 PID（内环）← 必做
                    ↓
                  PWM
```

- **外环不是「角度环」**：循迹用 **线位置偏差**（`pid_line`），不是 IMU yaw。
- **角度环 / 距离环**：有明确需求再加，不阻塞遥控与循迹上线。

#### car-2wd 自平衡：直立环 + 速度环 + 角度环（分层）

两轮自平衡是 **欠驱动倒立摆**，必须有一层 **直立环** 持续修正倾角；速度环、转向环叠在直立环之上。典型级联（与 car-4wd 不同）：

```text
  遥控 steer ──────────────► 转向 / yaw 环（角度环）──┐
  遥控 throttle / 距离期望 ─► 速度环 / 距离环 ────────┤
                                                      ▼
                              直立环（倾角 → 左右轮基础力矩）
                                                      ▼
                              轮速环（可选，或直立输出直接 PWM）
                                                      ▼
                                                    PWM
```

| 层级 | 作用 | 说明 |
|------|------|------|
| **直立环（最核心）** | 目标倾角（通常 0°）→ 左右轮同向力矩 | 控制周期宜 **5~10 ms**（高于 car-4wd 的 20 ms）；强依赖 IMU roll/pitch 与陀螺 |
| **速度环** | 期望车速 → 微调直立环 **目标倾角**（略前倾=加速） | 与 car-4wd 共用 `pid` / `motion` 模块，但耦合方式不同 |
| **角度环（转向）** | steer → 左右轮 **差速** | 在直立环输出上叠加差分，不单独替代直立环 |
| **距离环** | 路程 / 超声波 → 速度期望 | 可选，与 car-4wd 类似 |

**实现策略**：先在 car-4wd 上完成 **§5 阶段 0~3**（`pid`、`motion`、编码器测速、PWM 标定），再在 **car-2wd 工程** 增加 `balance.c`（或 `chassis` 自平衡分支）与 NVS 参数 `pid_balance`（待定义）。姿态 API 已有：`attitude_get_euler()`、`attitude_update_step()`（见 [`attitude-tuning.md`](attitude-tuning.md)）。

**轴映射注意**：自平衡通常以 **pitch（或 roll，视 IMU 安装）** 为直立环反馈；须在 `attitude.c` 中确认 `ATTITUDE_IMU_REMAP` 与车体「前后倾」一致，与 car-4wd 循迹调试分开验收。

---

## 2. 现状盘点（2026-07）

### 2.1 已接通

| 模块 | 状态 | 关键路径 |
|------|------|----------|
| 电机 PWM + 方向 | 开环固定占空比 50% | `projects/car-4wd/board/src/motor.c` |
| 编码器计数 | M1/M2 硬件 QEI，M3/M4 软件 QEI | `encoder.c`、`bsp_sw_qei` |
| 循迹 ADC | 6 路采样就绪 | `line.c`、`APP_CTRL_PERIOD_MS=20` |
| IMU / 磁力计 / 姿态 | Mahony + Fusion，50 Hz | `Common/src/attitude.c`、`app.c` |
| NVS 参数框架 | `pid_speed`、`pid_line`、`kinematics`、`spd_limit` 已有默认值 | `Common/src/nvs.c` |
| 蓝牙遥控 | 开环 `proto_apply_drive` | `Common/src/proto.c` |
| 控制节拍 | `app_tmr` 1 ms 编码器轮询 + 20 ms `EVT_ID_TIMER` | `projects/car-4wd/main/app.c` |

### 2.2 待实现（`app.c` 中已标注 TODO）

```c
/* TODO: 编码器速度计算（cfg_encoder_count + cfg_kinematics） */
/* TODO: PID 控制器（cfg_pid_speed / cfg_pid_line） */
/* TODO: 输出电机 PWM（cfg_motor_rpm + cfg_spd_limit 限速） */
```

### 2.3 关键缺口

1. **`Motor_SetSpeed` 未使用 RPM 参数**：占空比恒为 500/1000，无法做速度闭环。
2. **无轮速估算**：仅有计数，无 RPM。
3. **无 PID 模块**：NVS 仅存参数，无运行时控制器。
4. **无循迹偏差解算**：ADC 已上报，未转为横向误差。
5. **运行模式未落地**：`NVS_RUN_MODE_*` 已定义，业务层未切换。

---

## 3. 控制架构

### 3.1 数据流（目标态）

```
                    ┌─────────────────────────────────────────┐
  遥控 throttle/steer ──► 运动学解算 ──► left/right RPM 设定      │
  循迹 line_error     ──► 循迹 PID   ──► turn 修正 ─────────────┤
                    └───────────────────┬─────────────────────────┘
                                        ▼
                              限速 / 限加速度（spd_limit）
                                        ▼
                         ┌──────────────┴──────────────┐
                         │  M1/M3 左  │  M2/M4 右    │  ← 四轮分组
                         └──────┬───────┴──────┬───────┘
                                ▼              ▼
                         轮速 PID ×4    （读 cfg_pid_speed）
                                ▼
                         PWM + 方向（Motor_SetSpeed）
                                ▲
                         编码器 Δcount → RPM（50 Hz）
```

### 3.2 控制周期

| 任务 | 周期 | 说明 |
|------|------|------|
| `bsp_sw_qei_poll_all` | 1 ms | M3/M4 软件编码器备份采样 |
| `app_on_timer` | **20 ms（50 Hz）** | 速度估算、PID、循迹、协议 DRIVE 执行 |
| 姿态 `attitude_update_step` | 20 ms | 与控制同频，暂不参与首版循迹 |

50 Hz 对 MG513 + 减速箱足够；若单轮 PID 振荡可试 10 ms（需评估 CPU 与 ADC 负载）。

### 3.3 坐标与电机分组（car-4wd）

与 `proto_apply_drive` 一致：

| 电机 ID | 位置 | 编码器 index |
|---------|------|--------------|
| M1 | 左前 | 0 |
| M2 | 右前 | 1 |
| M3 | 左后 | 2 |
| M4 | 右后 | 3 |

左右轮速：

- `left_rpm` → M1、M3（经 `cfg_motor_rpm` 极性修正）
- `right_rpm` → M2、M4

### 3.4 轮速换算公式

NVS 默认（`nvs_cfg_apply_defaults`）：

- `encoder_cpr = 11`（电机轴每圈脉冲）
- `gear_ratio = 30`
- 正交 4 倍频：`pulses_per_wheel_rev = encoder_cpr × gear_ratio × 4`

```text
rpm = (delta_counts / pulses_per_wheel_rev) × (60000 / period_ms)
```

实现时封装为 `motion_count_to_rpm(delta, period_ms)`，参数来自 `cfg_kinematics()`。

---

## 4. 模块规划

建议新增 **Common 层** 模块，板级仍只提供 `Motor_*` / `Encoder_*` / `Line_*`：

| 模块 | 路径 | 职责 |
|------|------|------|
| `pid` | `Common/inc/pid.h`、`Common/src/pid.c` | 通用 PID（位置式/增量式二选一），抗积分饱和，输出限幅 |
| `motion` | `Common/inc/motion.h`、`Common/src/motion.c` | 轮速估算、RPM↔PWM 标定、差速运动学、限速/限加速度 |
| `line_follow` | `Common/inc/line_follow.h`、`Common/src/line_follow.c` | ADC→偏差、丢线检测、循迹 PID 外环 |
| `chassis` | `Common/inc/chassis.h`、`Common/src/chassis.c` | 模式状态机、统一 `chassis_tick(period_ms)`、对接 proto/cmd |
| `balance` | `Common/inc/balance.h`、`Common/src/balance.c` | **仅 car-2wd 自平衡**：直立环 + 与速度/转向环级联（§5.9） |

`app_on_timer` 中仅调用：

```c
chassis_tick(APP_CTRL_PERIOD_MS);
```

---

## 5. 分阶段实现顺序

原则：**先内环、后外环；先单轮、后整车；先开环标定、再闭环；每阶段可独立验收**。

---

### 阶段 0：开环电机标定（前置）

**目的**：建立 RPM ↔ PWM 映射，否则速度 PID 无意义。

| 项 | 内容 |
|----|------|
| 改动 | `motor.c`：`Motor_SetSpeed` 按 RPM 绝对值映射占空比（分段线性或查表）；0 RPM 刹车/滑行策略明确 |
| 标定 | UART7 `motor <id> <rpm>` 手测各电机；记录最小启动占空比、满速 RPM@占空比 |
| 参数 | 可选 NVS 键 `mot_cal`（后续）；首版可用编译期常量 |
| 验收 | 固定 RPM 命令下四轮转向正确、转速大致一致；`cfg_motor_rpm` 极性 mask 可通过厂测/CMD 配置 |

**依赖**：无。  
**阻塞**：阶段 2 及之后全部。

---

### 阶段 1：编码器测速

**目的**：50 Hz 输出各轮 RPM，供日志与 PID 反馈。

| 项 | 内容 |
|----|------|
| 改动 | `motion.c`：`motion_encoder_update()` 读 `cfg_encoder_count(i)` 差分 |
| 滤波 | 一阶低通 `alpha=0.3`（`motion.c`）；M1/M2 硬件 QEI 已启用数字滤波 |
| 日志 | `app: enc rpm=[...]` 周期 500 ms（与现有 line 日志风格一致） |
| 协议 | 遥测已有编码器计数；可增 RPM 字段或保持上位机自行差分 |
| 验收 | 手转单轮，RPM 符号与转速量级合理；停车 0 RPM |

**依赖**：阶段 0 可并行，但与阶段 2 联调前需完成本阶段。

---

### 阶段 2：单轮速度 PID

**目的**：验证 PID 框架与参数通路。

| 项 | 内容 |
|----|------|
| 改动 | `pid.c`；`chassis.c` 初版仅支持 `CHASSIS_MODE_SPEED_TEST` |
| 逻辑 | 单电机目标 RPM（CMD `motor` 扩展或 `spd_test <id> <rpm>`），其余三轮 0 |
| 参数 | `cfg_pid_speed()`；CMD `param pid_spd Kp Ki Kd` 已存在 |
| 验收 | 阶跃 100→200 RPM：上升时间、超调可接受；积分 windup 不明显 |

**依赖**：阶段 0 + 阶段 1。

---

### 阶段 3：四轮独立速度环 + 限幅

**目的**：内环完整，支持左右不同设定。

| 项 | 内容 |
|----|------|
| 改动 | 4 路 PID 实例；`cfg_spd_limit()`：`max_rpm`、`max_accel_rpm_s` |
| 逻辑 | 设定值斜坡限制；输出限幅 ±max_rpm；PWM 饱和时 anti-windup |
| 分组 | 同侧两轮同目标（左：M1+M3，右：M2+M4），或暂允许四轮同值再收紧 |
| 验收 | 四轮同速直线：编码器 RPM 差 < 10%；急停无失控 |

**依赖**：阶段 2。

---

### 阶段 4：遥控外环（蓝牙 DRIVE 闭环化）

**目的**：替换 `proto_apply_drive` 开环直驱。

| 项 | 内容 |
|----|------|
| 改动 | `proto.c`：`proto_apply_drive` 改为写 `chassis_set_drive(throttle, steer)`；`chassis_tick` 内执行运动学 |
| 逻辑 | 与现协议一致：`left = base - turn`，`right = base + turn`，再进阶段 3 速度环 |
| 安全 | 保留 DRIVE 超时停车；`proto_motor_all_stop` 清 PID 积分 |
| 模式 | `NVS_RUN_MODE_REMOTE` |
| 验收 | 蓝牙遥控直线/转弯平滑；丢连接 500 ms 内停车 |

**依赖**：阶段 3。

---

### 阶段 5：循迹偏差与阈值

**目的**：6 路 ADC → 横向误差，暂不控车。

| 项 | 内容 |
|----|------|
| 改动 | `line_follow.c`：`line_follow_compute_error(adc[6])` |
| 算法 | 推荐加权重心：对低于 `cfg_line_threshold(i)` 的通道赋权重 `-5,-3,-1,+1,+3,+5`（按板子左→右顺序校准） |
| 丢线 | 全白/全黑计数超阈值 → `LINE_STATE_LOST` |
| 标定 | 厂测或 CMD 写 `line_th`；黑线/白底实测阈值 |
| 日志 | `app: line err=<f> state=TRACK\|LOST` |
| 验收 | 手移小车对准黑线，误差符号正确、零点居中 |

**依赖**：可与阶段 3 并行开发；联调需阶段 3。

---

### 阶段 6：循迹闭环

**目的**：自主沿黑线。

| 项 | 内容 |
|----|------|
| 改动 | 外环 PID 用 `cfg_pid_line()`；基准速度 `line_base_rpm`（NVS 或常量，如 80 RPM） |
| 逻辑 | `turn = pid_line(error)`；`left = base - turn`，`right = base + turn`；丢线 → 减速停车 |
| 模式 | `NVS_RUN_MODE_LINE_FOLLOW` |
| 验收 | S 弯、90° 弯不丢线（可调）；丢线停车 |

**依赖**：阶段 4 + 阶段 5。

---

### 阶段 7：运行模式与按键

**目的**：用户可切换 IDLE / REMOTE / LINE_FOLLOW。

| 项 | 内容 |
|----|------|
| 改动 | `app_on_button`；可选 proto 命令；`nvs_param_set_last_mode` |
| 逻辑 | 切换时清 PID 积分、电机停；LED/蜂鸣器提示 |
| 验收 | 按键循环模式；重启恢复 `last_mode`（可选） |

**依赖**：阶段 4 + 阶段 6。

---

### 阶段 8：car-4wd 增强（可选，后续迭代）

按优先级排序：

1. **同侧双轮差速修正**：左右侧 M1/M3、M2/M4 长期 RPM 差 > 阈值时微调。
2. **航向保持（角度环）**：直线段用 `attitude yaw` 辅助 `turn`（磁干扰场景降权）。
3. **编码器里程计（距离环基础）**：累计距离，协议上报；可扩展为定距停车。
4. **超声波减速（距离环）**：`Board_Ultra_*` 接入外环限速。
5. **car-2wd 地面差速**：`track_width_m=0` 时两轮差速遥控/循迹（**非自平衡**，仍无直立环）。

**依赖**：阶段 7 完成；与阶段 9 无先后要求，但 **阶段 9 依赖阶段 0~3 的 pid/motion**。

---

### 阶段 9：car-2wd 两轮自平衡（直立环 + 速度 / 转向）

**目的**：在 **car-2wd 自平衡硬件** 上实现直立不倒，并可遥控前进与转向。  
**前提**：car-4wd（或 car-2wd 台架）已完成 **阶段 0~3**（PWM 标定、测速、`pid.c`）。

| 项 | 内容 |
|----|------|
| 改动 | `Common/src/balance.c`（或 `chassis` 的 `CHASSIS_MODE_BALANCE`）；`projects/car-2wd/main/app.c` 控制周期改为 **5~10 ms** |
| **直立环** | IMU 倾角 + 角速度 → `pid_balance` → 左右轮同向基础 PWM；倾角超限 → 电机停并告警 |
| **速度环** | throttle → 倾角期望偏移（或速度 PID 输出叠到直立环 setpoint） |
| **角度环** | steer → 左右轮差速，叠加在直立环输出上 |
| **距离环** | 可选：编码器路程 / 超声波，与 §5.8 相同 |
| 参数 | 新增 NVS `pid_balance`（待 schema 扩展）；直立 / 速度 / 转向分环整定 |
| 安全 | 倾角 > 阈值（如 ±25°）立即 `Motor_SetSpeed(0)`；仅遥控小角时使能直立 |
| 验收 | 手扶启动后松手 **5 s 内** 稳定直立；原地转向；低速直线 1 m 不倾覆 |

**依赖**：阶段 0~3（共用 PID 与测速）；`attitude` 轴映射针对 car-2wd 自平衡单独验收（§1.3）。  
**与 car-4wd 关系**：**直立环代码不在 car-4wd 启用**；`device_profile` / 编译目标区分车型。

```text
阶段9 依赖：阶段0~3（pid/motion）
阶段9 并行：阶段4~8（car-4wd 业务可继续做，互不阻塞）
```

---

## 6. 阶段总览（甘特式）

```text
阶段0 开环标定      ████
阶段1 编码器测速      ████
阶段2 单轮PID            ████
阶段3 四轮速度环             ████
阶段4 遥控外环                   ████
阶段5 循迹偏差        ████（可与2~3并行）
阶段6 循迹闭环                         ████
阶段7 模式/按键                            ████
阶段8 4wd增强                               ····→
阶段9 2wd自平衡(直立环)              ····→（依赖 0~3，与 4~8 可并行）
```

**关键路径（car-4wd）**：0 → 1 → 2 → 3 → 4 → 6 → 7。  
**关键路径（car-2wd 自平衡）**：0 → 1 → 2 → 3 → **9**（直立环 + 速度/转向）。

---

## 7. 参数与调参

### 7.1 NVS 默认值（当前固件）

| 参数 | 默认值 | 用途 |
|------|--------|------|
| `pid_speed` | Kp=1, Ki=0, Kd=0 | 轮速环（需实机整定） |
| `pid_line` | Kp=2, Ki=0, Kd=0.1 | 循迹外环 |
| `max_rpm` | 300 | 遥控/循迹上限 |
| `max_accel_rpm_s` | 600 | 设定值斜坡 |
| `wheel_diam_m` | 0.065 | 速度换算、里程 |
| `gear_ratio` | 30 | 同上 |
| `encoder_cpr` | 11 | 同上 |
| `track_width_m` | 0.18（4wd） | 转向几何 |
| `line_threshold[]` | 2048 | 循迹二值化 |

### 7.2 调参顺序建议

1. **阶段 0**：PWM 标定（无 PID）。
2. **阶段 2**：仅 Kp，小步增加至轻微振荡再回退 20%。
3. **阶段 2~3**：加 Kd 抑制超调；Ki 最后加，解决静差。
4. **阶段 6**：循迹 Kp 从小开始；Kd 抑制摆动；基准速度先低后高。

### 7.3 调试接口

| 接口 | 命令/手段 |
|------|-----------|
| UART7 CMD | `motor`、`param pid_spd`、`param pid_line`、`param max_rpm` |
| 日志 | `app: enc rpm`、`app: line err`、现有 `app:att` |
| 蓝牙 | 协议写 PID、订阅编码器/循迹 ADC |
| 厂测 | 电机项、编码器计数、循迹 ADC |

---

## 8. 验收用例（整机）

| # | 场景 | 通过标准 |
|---|------|----------|
| 1 | 四轮同速 100 RPM | 10 s 内四轮 RPM 均值差 < 15% |
| 2 | 蓝牙直线 3 m | 无明显跑偏；可接受小修正 |
| 3 | 蓝牙原地转 | 左右轮反向；切换平滑 |
| 4 | 循迹直线 + 弯 | 不丢线；最大横向偏差 < 1 传感器间距 |
| 5 | 循迹丢线 | 1 s 内停车 |
| 6 | 遥控断连 | 超时停车，无持续输出 |
| 7 | 低占空比启动 | 不抖动、不啸叫 |

---

## 9. 风险与注意事项

| 风险 | 缓解 |
|------|------|
| 编码器边沿质量 | 板级 **已去 RC 滤波**；M1/M2→硬件 QEI（QEI1/QEI0），M3/M4→`bsp_sw_qei`；M1/M2 差异大时先查 `Encoder_GetCount` 是否随转轮变化 |
| `Motor_SetSpeed` 占空比非线性 | 阶段 0 查表；PID 输出映射到占空比而非假 RPM |
| ADC 循迹与电池/按键争用 | 循迹走 **ADC0**，已在板级拆分；保持 `Line_Sample` 仅在 20 ms 任务调用 |
| I2C 与实时性 | 姿态 50 Hz 已运行；控制环不阻塞 I2C |
| 堆栈/堆 | 新增模块后查看 `app: heap free`；PID 状态放静态区 |
| car-2wd 地面 vs 自平衡 | 地面差速无直立环；自平衡需 **5~10 ms** 周期与 `balance.c`，见 §1.3、§5.9 |
| 自平衡倾角轴错误 | 单独验收 `ATTITUDE_IMU_REMAP`；pitch/roll 与「前后倾」一致再调 `pid_balance` |
| 自平衡启动 | 首版可手扶启动；倾角超限必须断电机 |

---

## 10. 相关文件索引

| 路径 | 说明 |
|------|------|
| `projects/car-4wd/main/app.c` | 20 ms 控制入口、TODO 挂载点 |
| `projects/car-4wd/board/src/motor.c` | PWM/方向（待 RPM 映射） |
| `projects/car-4wd/board/src/encoder.c` | 计数 |
| `projects/car-4wd/board/src/line.c` | 循迹 ADC |
| `Common/src/cfg.c` | NVS 业务访问 |
| `Common/src/nvs.c` | 默认 PID/运动学 |
| `Common/src/proto.c` | 蓝牙 DRIVE、遥测 |
| `Common/src/cmd.c` | 调试命令 |
| `Common/src/start.c` | 电机/编码器/循迹 init 顺序 |
| `docs/attitude-tuning.md` | 姿态（循迹与 **自平衡直立环** 均依赖） |
| `projects/car-2wd/main/app.c` | 两轮应用；自平衡时缩短控制周期 |

---

## 11. 修订记录

| 日期 | 说明 |
|------|------|
| 2026-07-12 | 初版：闭环分层、八阶段实现顺序与验收标准 |
| 2026-07-12 | 增 §1.3 控制环选型；car-2wd 自平衡 §5.9 直立环阶段；标题覆盖双车型 |
