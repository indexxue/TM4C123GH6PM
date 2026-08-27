# 遥控器框架重设计计划

**版本**：0.2（已确认）  
**日期**：2026-08-27  
**产品**：`projects/rc-controller`  
**状态**：决策已锁定，可按 R1 起分阶段实施  
**前置文档**：[rc-joystick-menu-design.md](./rc-joystick-menu-design.md)、[bluetooth-protocol.md](./bluetooth-protocol.md)

---

## 1. 背景与目标

阶段 A + B1 已可用，但 `rc_ui.c`（~1200 行）耦合 UI 模式、输入、协议与安全门控，扩展成本高。本次重设计目标：

| 目标 | 说明 |
|------|------|
| **主动建链** | 用户按 JS1 发起连接/进控，而非上电即自动 HELLO |
| **JS2 职责** | 优先用于 **控制模式切换** 与 **目标设备切换** |
| **多模型** | 多套映射/曲线/输入源，NVS 持久化（对标 EdgeTX Model） |
| **IMU 操控** | 新增 MPU6050 倾斜操控模式（手柄姿态 → throttle/steer） |
| **结构拆分** | 四层架构，便于后续接 JS2 业务、循迹/云台协议 |
| **暂不做 NRF24** | 链路层只抽象接口，近期仅蓝牙 `proto_client` 后端 |

**不在本期**：NRF24 驱动、Expo 全套 UI、完整混控编辑器、独立导航键。

---

## 2. 开源「主动连接」调研结论

### 2.1 常见做法对比

| 项目 / 协议 | 连接方式 | 是否有「按键发起」 |
|-------------|----------|-------------------|
| **[OpenRC-STM32](https://github.com/EbrSiami/OpenRC-STM32)** | NRF24 固定地址，上电即 500 Hz 发包 | **无**；地址预配置，链路指示靠 RX 统计 |
| **EdgeTX / OpenTX** | 模块 Bind（FrSky/Crossfire 等） | **有**；菜单进入 Bind 模式，物理确认 |
| **ESP-NOW 类项目** | Peer MAC 交换 | **有**；长按进入 Pairing，交换 MAC |
| **HC-05 + SPP** | 蓝牙模块层配对，串口透传 | 模块层一次配对；应用层无标准 |
| **本仓库现状** | `proto_client_tick` 每 1 s 自动发 HELLO | **半自动**；有 RX 即 `link_up`，与用户意图无关 |

**结论**：开源 RC 在 **射频层** 很少用摇杆「一键连接」；**应用层握手**（Bind / Pair / HELLO）才接近「用户确认后再通」。我们可在 **协议 HELLO 之上** 做「手动建链」，与 EdgeTX Bind 思路一致，而不改 HC-05 配对。

### 2.2 本方案：两层连接模型

```
┌─────────────────────────────────────────────────────────┐
│ L1 蓝牙 SPP（HC-05）     模块已配对即 UART 字节流通      │
├─────────────────────────────────────────────────────────┤
│ L2 应用协议（HELLO）     用户 JS1 触发 → 握手 → LINK OK  │
└─────────────────────────────────────────────────────────┘
```

| 状态 | 行为 |
|------|------|
| **DISCONNECTED** | 不发 HELLO/DRIVE；顶栏 `LINK --`；JS1 短按 → 进入 CONNECTING |
| **CONNECTING** |  burst HELLO（如 200 ms × 5）；收到 HELLO ACK → CONNECTED |
| **CONNECTED** | 维持 PING；JS1 短按（回中）→ 进入 DRIVE 会话 |
| **DRIVE** | 按当前模型输入源发 DRIVE；JS1 回中短按 → 退会话回 HOME，**保持 CONNECTED**（不断 LINK） |

**与现状差异**：`proto_client_tick` 改为 **默认不自动 HELLO**；仅 `rc_link_connect()` 或 JS1 触发后才开始握手。

---

## 3. 目标架构（四层）

```
                    ┌──────────────┐
   ADC / IMU / 按键 │  rc_input   │ 50 Hz 采样 + 校准 + 边沿
                    └──────┬───────┘
                           │ rc_input_snapshot_t
                    ┌──────▼───────┐
                    │  rc_mixer    │ 多模型 → 8 通道 [-1000,+1000]
                    └──────┬───────┘
                           │ rc_channels_t
         ┌─────────────────┼─────────────────┐
         ▼                 ▼                 ▼
  ┌─────────────┐   ┌─────────────┐   ┌─────────────┐
  │  rc_link    │   │  rc_screen  │   │  rc_model   │
  │  (BT 后端)  │   │  (UI 栈)    │   │  (NVS)      │
  └─────────────┘   └─────────────┘   └─────────────┘
```

### 3.1 模块落点

| 模块 | 路径 | 职责 |
|------|------|------|
| `rc_input` | `projects/rc-controller/source/rc_input.{h,c}` | 摇杆采样、按键状态机、IMU 采样封装 |
| `rc_mixer` | `projects/rc-controller/source/rc_mixer.{h,c}` | 输入源 → 通道；Expo/DualRate（后期） |
| `rc_model` | `projects/rc-controller/source/rc_model.{h,c}` | 多模型 CRUD、NVS blob、当前模型索引 |
| `rc_link` | `projects/rc-controller/source/rc_link.{h,c}` | 建链状态机；封装 `proto_client` |
| `rc_screen_*` | `projects/rc-controller/source/screen/` | 每屏一文件；Screen Stack |
| `joy_cal` | 保留 | 校准 NVS，供 `rc_input` 调用 |
| `app.c` | 薄编排 | tick 顺序：input → mixer → link → screen |

**删除/瘦身**：`rc_ui.c` 拆完后仅留兼容 shim 或删除。

---

## 4. 多模型（Model）

### 4.1 概念

对标 EdgeTX **Model**：每套模型独立保存输入映射、曲线、订阅 mask、目标设备槽位。

| 字段 | 说明 |
|------|------|
| `name[16]` | 显示名，如 `Race` / `Line` / `Tilt` |
| `input_src` | `STICK` / `IMU_TILT` |
| `map_throttle` | 通道 0 来源轴（J1Y / pitch / …） |
| `map_steer` | 通道 1 来源轴（J1X / roll / …） |
| `expo[2]` | throttle / steer 指数（0=线性，后期） |
| `dual_rate` | 全行程比例（后期） |
| `sub_mask` | 该模型默认遥测订阅 |
| `target_slot` | 绑定的目标设备槽 0..N-1 |
| `imu_zero_roll/pitch` | IMU 模式零点（进入时校准） |
| `imu_sens` | 倾斜灵敏度 ×1000 |

**默认 3 个模型（出厂）**：

| # | 名称 | input_src | 说明 |
|---|------|-----------|------|
| 0 | `Stick` | STICK | JS1 Y/X → throttle/steer |
| 1 | `Line` | STICK | 同上；sub 含循迹相关（后期映射 JS2） |
| 2 | `Tilt` | IMU_TILT | 手柄 pitch/roll → throttle/steer |

### 4.2 NVS

| 键 | 内容 |
|----|------|
| `rc/models` | blob：`magic + version + count + rc_model_t[]` + CRC32 |
| `rc/model_idx` | 当前模型索引 u8 |
| `rc/targets` | 目标设备表（见 §5） |
| `rc/target_idx` | 当前目标槽 u8 |
| `cal/joy` | 保留现有校准 blob |

单模型约 48 B；**上限 8 模型**（已确认）≈ 400 B，在 NVS 限额内。

```c
#define RC_MODEL_MAX   8U
```

---

## 5. 目标设备（Target Device）

JS2 负责 **模式/目标** 切换（见 §6）。目标设备指「要连哪台小车」。

### 5.1 槽位表

```c
typedef struct {
    char     nickname[12];   /* 显示 "Car-A" */
    char     serial[16];     /* 与小车 NVS serial 一致，空=任意 */
    uint32_t last_caps;      /* 上次 HELLO 缓存，UI 用 */
} rc_target_t;
```

**默认 4 槽**，serial 空表示「接受首个 HELLO 应答」。

### 5.2 协议扩展（v2.1，R3 实施，已确认）

当前 HELLO ACK 含 `fw_version + hw_rev + caps`，**不含 serial**。多目标需：

| 方向 | 扩展 |
|------|------|
| HELLO 请求（可选 payload） | `[target_serial 16B]`，全 0 = 广播/任意 |
| HELLO 应答 | 追加 `serial[16]`（小车 `nvs_cfg.serial`） |

遥控器 CONNECTING 时带当前 target 的 serial；应答 serial 不匹配 → 拒绝建链，提示 `WRONG DEV`。

**车端改动**：`Common/src/proto.c` `proto_handle_hello` 应答追加 serial；兼容旧主机（无 payload）。

### 5.3 与 HC-05 的关系

- 多车若各用独立 HC-05，通常 **蓝牙层已绑定不同模块**，切换 = 用户手动切 BT 连接或未来多模块。
- 同一条 SPP 链路上多设备极少见；**serial 过滤** 主要防误连、支持「先连上再识别」。

---

## 6. JS1 / JS2 新交互模型

### 6.1 JS1 — 连接与会话

| 上下文 | JS1 短按 | 条件 |
|--------|----------|------|
| HOME + DISCONNECTED | **发起建链** → CONNECTING | — |
| HOME + CONNECTED | **进入 DRIVE** | 四轴死区内 |
| DRIVE | **退出 DRIVE** → HOME（**LINK 保持 CONNECTED**） | 须回中 |
| CONNECTING | 取消建链 | — |
| MENU / CAL | ENTER（不变） | — |

长按 JS1：预留「断开连接」→ DISCONNECTED（可选，二期）。

### 6.2 JS2 — 模式与目标

| 操作 | 效果 |
|------|------|
| **JS2 短按** | 循环 **控制模式**（当前模型 `input_src`：STICK ↔ IMU_TILT，或模型列表内下一项） |
| **JS2 长按 ≥1.5 s** | 打开 **Target 选择 overlay**（已确认） |
| DRIVE 中 JS2 短按 | 切换 **模型**（**先 mute DRIVE**，切换后保持 DRIVE 会话） |
| MENU 中 | 保持现有 BACK / 导航 |

**已定交互**：

- JS2 **短按** = 循环切换 **模型**（最多 8 个：Stick → Line → Tilt → …）
- JS2 **长按** = **目标设备** 列表（上下选，JS1 确认）
- **Settings**（校准/死区/About）：HOME 下 **JS1+JS2 同时短按** 进入（原 JS2 长按职责已让给 Target）

### 6.3 安全

| 规则 | 说明 |
|------|------|
| 切换模型/目标 | 先发 `DRIVE_STOP`，mute ≥1 帧 |
| IMU 模式进入 | 静止 0.5 s 采零点；未校准不允许 arm |
| MENU/CAL | 禁发有效 DRIVE（发 0） |

---

## 7. IMU 倾斜操控模式（MPU6050）

### 7.1 硬件与软件基础

- 遥控器板：**MPU6050 @ I2C0 0x69**（与小车相同驱动 `Common/src/imu.c`）
- 当前 `app.c` 已初始化 IMU；`RC_MAG_ENABLE=0`，**无磁力计**
- 姿态：**6-DOF** 即可（roll/pitch 控车；yaw 漂移不影响 throttle/steer 映射）

### 7.2 信号链

```
imu_read_sample()
    → attitude_update_from_imu()     /* 50 Hz，与 car 相同 Fusion 路径 */
    → roll/pitch 减零点 (model.imu_zero_*)
    → 限幅 + 灵敏度 (model.imu_sens)
    → 可选 deadband（角度 °×10）
    → rc_mixer ch[0] throttle, ch[1] steer
    → rc_link → DRIVE
```

**默认映射**（可 per-model 配置 invert）：

| 手柄动作 | 通道 | 符号 |
|----------|------|------|
| 前倾（pitch +） | throttle | 前进 |
| 后倾（pitch −） | throttle | 后退 |
| 左倾（roll −） | steer | 左转 |
| 右倾（roll +） | steer | 右转 |

### 7.3 进入 IMU 模式流程

1. 用户 JS2 切到 `Tilt` 模型，或当前模型 `input_src=IMU_TILT`
2. 屏提示 **Hold level**（0.5 s）
3. 采样 `imu_zero_roll/pitch`，写入 **运行时**（可选写回 model NVS）
4. HOME 十字区改为 **姿态球/水平仪** 小部件（`lcd_panel` 扩展）
5. JS1 进 DRIVE 后按倾斜发控

### 7.4 验收

| 项 | 期望 |
|----|------|
| 水平持握 | throttle/steer ≈ 0 |
| 前倾 15° | throttle 单调增 |
| 切换回 STICK | 立即恢复摇杆，无 IMU 残留 |
| 菜单/校准 | 不读 IMU 进 DRIVE |

### 7.5 风险

| 风险 | 缓解 |
|------|------|
| PCB 安装方向与 `ATTITUDE_IMU_REMAP` 不符 | 遥控器独立 `rc_imu_remap` 或菜单「轴向校准」 |
| 行走中切换 IMU | 强制静止零点 |
| SPI 屏 + I2C IMU 同周期 | IMU 与 LCD 分频（IMU 50 Hz，屏 20 Hz） |

---

## 8. UI：Screen Stack

```
HomeScreen          双十字/水平仪 + BAT + LINK + 模型名 + 目标名
  ├ overlay TargetPickerScreen    JS2 长按 ≥1.5 s
  ├ push DriveScreen              JS1 arm（HUD 小十字 + 遥测）
  └ overlay MenuScreen            HOME 下 JS1+JS2 同时短按进入 Settings
CalScreen           仅从菜单进入
```

**DriveScreen 改进**：保留角落 HUD 十字/水平仪，不全屏文字遥测。

---

## 9. rc_link 接口（蓝牙后端）

```c
typedef enum {
    RC_LINK_OFF,
    RC_LINK_CONNECTING,
    RC_LINK_CONNECTED,
} rc_link_state_t;

status_t rc_link_init(void);
void     rc_link_tick(uint32_t dt_ms);

rc_link_state_t rc_link_state(void);
status_t rc_link_connect(void);      /* JS1：开始 HELLO burst */
status_t rc_link_disconnect(void);   /* 可选：停 HELLO/PING */

status_t rc_link_send_drive(const int16_t ch[2]); /* throttle, steer */
bool_t   rc_link_armed(void);        /* DRIVE 会话 */

void rc_link_telem_get(proto_client_telem_t *out); /* 薄封装 */
```

近期 **不实现** `rc_link_nrf24.c`。

---

## 10. 实施分期

| 阶段 | 内容 | 依赖 | 预估 |
|------|------|------|------|
| **R1** | 拆分：`rc_input` + `rc_link`（手动建链）+ `screen/home`；**行为对齐现版**除「上电自动 LINK」改为 JS1 建链 | — | 3 d | **已完成** |
| **R2** | `rc_model` + NVS；JS2 短按切模型；Settings 增 Model 列表 | R1 | 2 d | **已完成** |
| **R3** | 协议 HELLO 带/回 serial；`rc_target` + JS2 长按 Target overlay | R2 + 车端小改 | 2 d | **已完成** |
| **R4** | IMU 模式：`rc_input` IMU 源 + 零点 + `Tilt` 模型 + 水平仪 UI | R2 | 3 d | **已完成** |
| **R5** | Drive HUD 重构；订阅 overlay；`rc_ui.c` 删除 | R1–R4 | 2 d | **已完成** |
| **R6** | Expo / Dual Rate（可选） | R2 | 2 d |

**建议执行顺序**：R1 → R2 → R4（IMU 与多模型可并行设计）→ R3 → R5。

### R1 验收

- [ ] 上电 `LINK --`，不发 HELLO
- [ ] JS1 短按 → CONNECTING → LINK OK
- [ ] JS1 再短按（回中）→ DRIVE；现有禁发/菜单逻辑不变
- [ ] `rc_ui.c` 行数减半以下

### R2 验收

- [ ] ≥3 模型可切换，重启保持
- [ ] JS2 短按切换模型，屏显模型名

### R3 验收

- [x] HELLO ACK 含 16B serial（车端 `proto_handle_hello`）
- [x] 4 槽位 NVS `rc/targets`；JS2 长按 Target overlay
- [x] 槽位已绑定 serial 时仅接受匹配设备；否则 tip `WRONG DEV`
- [x] 首次连上未绑定槽位自动写入 peer serial

### R5 验收

- [x] DRIVE 页：Bat/US/Spd 三行 + 右下角 CMD 十字（非全屏文字遥测）

### R4 验收

- [ ] Tilt 模型：静止零点 + 倾斜控车
- [ ] 与 Stick 模型可热切换（mute 安全）

---

## 11. 文件结构（目标）

```
projects/rc-controller/source/
├── rc_input.{h,c}
├── rc_mixer.{h,c}
├── rc_model.{h,c}
├── rc_link.{h,c}
├── joy_cal.{h,c}          # 保留
├── rc_sub.{h,c}           # 保留，sub mask 迁入 model 后可选合并
├── rc_lcd_cfg.h
└── screen/
    ├── rc_screen.h        # stack + push/pop
    ├── screen_home.c
    ├── screen_drive.c
    ├── screen_menu.c      # 复用 components/menu
    ├── screen_cal.c
    └── screen_target.c
```

---

## 12. 已确认决策（2026-08-27）

| # | 问题 | 结论 |
|---|------|------|
| 1 | JS2 长按 | **Target 选择 overlay**；Settings → HOME 下 **JS1+JS2 同时短按** |
| 2 | 退出 DRIVE 后 LINK | **保持 CONNECTED**，仅 disarm 回 HOME |
| 3 | HELLO serial 扩展 | **是**，与 R3 多目标一起做（车端 `proto_handle_hello` 应答追加 serial） |
| 4 | 模型数量上限 | **8**（`RC_MODEL_MAX`） |
| 5 | IMU remap | 仍待定：实机试后定 `RC_IMU_REMAP` 或菜单轴向校准（不阻塞 R1–R2） |

---

## 13. 参考链接

- [OpenRC-STM32](https://github.com/EbrSiami/OpenRC-STM32) — 分层、NRF24、500 Hz（链路参考，本期不移植 RF）
- [EdgeTX](https://github.com/EdgeTX/edgetx) — Model、校准、Bind 交互
- 仓库 [rc-joystick-menu-design.md](./rc-joystick-menu-design.md) — 现有校准/菜单约定

---

## 14. 变更记录

| 版本 | 日期 | 说明 |
|------|------|------|
| 0.1 | 2026-08-27 | 初稿：主动建链、多模型、JS2 职责、IMU 模式、分期计划 |
| 0.2 | 2026-08-27 | 锁定决策：JS2 长按=Target、DRIVE 退出保持 LINK、R3 serial、模型上限 8 |
| 0.3 | 2026-08-27 | R1 落地：rc_input / rc_link / screen_home / screen_target 占位 |
| 0.4 | 2026-08-27 | R2 落地：rc_model NVS、JS2 短按切模型、Settings 模型/订阅 |
| 0.5 | 2026-08-27 | R4 落地：rc_mixer IMU 倾斜控车、零点校准、水平仪 |
| 0.6 | 2026-08-27 | R3/R5：HELLO serial、rc_target、Target overlay、Drive HUD 十字 |
