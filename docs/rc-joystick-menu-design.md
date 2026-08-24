# 遥控器摇杆校准与菜单可视化设计

**版本**：1.1  
**日期**：2026-08-24  
**产品**：`projects/rc-controller`  
**状态**：阶段 A + B1 已落地（联调通过：屏向/色序/双杆校准 UI + 行程门槛）  
**使用说明**：[rc-controller-usage.md](./rc-controller-usage.md)

**参考**：EdgeTX / OpenTX 摇杆校准向导；OpenRC-STM32（min/center/max + deadband + NVS）；仓库内 `components/menu`。

---

## 1. 目标与范围（阶段 A）

| 做 | 不做（后续） |
|----|--------------|
| 双杆一起校准向导（中心 → 极限 → 写 NVS） | Expo / Dual Rate / EPA |
| NVS 持久化校准 + 死区 + 轴反转 | js2 业务通道映射（云台/模式等） |
| 主屏双十字方向可视化 | 完整混控 / 多模型配置 |
| 菜单：校准、通道监视、死区、反转、恢复默认 | 独立导航键 / 编码器 |
| 菜单/校准期间禁发 `DRIVE` | |

**验收重点**：主屏十字方向与物理推杆一致；校准后两端能稳定到 ±1000；回中死区内 cmd≈0。

---

## 2. 硬件与既有基础

| 项 | 约定 |
|----|------|
| 屏 | ST7789 1.14"，240×135 横屏 |
| JS1 | ADC PD0(X)/PD1(Y)，按键 PC4 |
| JS2 | ADC PD2(X)/PD3(Y)，按键 PB6 |
| 轴向（实测） | X：左=0 … 中≈2048 … 右≈4095；Y：下=0 … 中≈2048 … 上≈4095 |
| 驱动量纲 | cmd ∈ [-1000, +1000] |
| 当前映射 | JS1 Y→throttle，JS1 X→steer；JS2→`aux` 占位 |
| 菜单引擎 | `components/menu`（`MENU_EVT_*`） |
| 持久化 | Common NVS，`ns=cal`，blob ≤128 B |

---

## 3. 信号处理流水线

```
ADC raw (12-bit)
    → (可选) EMA 轻滤波
    → 校准表 map：按轴 min/center/max 分段线性 → [-1000, +1000]
    → 死区 deadband（中心附近强制 0）
    → 轴反转 invert
    → 应用层：JS1 → DRIVE(throttle, steer)；JS2 → aux（占位）
```

校准写入的 `min/center/max` **替代**当前硬编码的 `BOARD_JOY_ADC_*` 默认值；无有效 NVS 时回退固件默认（0 / 2048 / 4095 + 现有 edge 预留逻辑）。

### 3.1 分段映射（与 EdgeTX 同类）

对单轴 raw：

- `raw ≤ center`：映射到 `[-cmd_max, 0]`，跨度 `(center - min)`，扣除死区
- `raw ≥ center`：映射到 `[0, +cmd_max]`，跨度 `(max - center)`，扣除死区
- 非法包（`min ≥ center` 或 `center ≥ max` 等）→ 丢弃，用默认

---

## 4. NVS 数据

**键**：`ns = "cal"`，`key = "joy"`（单 blob，一次 commit）。

```c
typedef struct {
    uint16_t magic;       /* 0x4A43 ('J''C') */
    uint16_t version;     /* 1 */
    struct {
        uint16_t min;
        uint16_t center;
        uint16_t max;
    } axis[4];            /* 0:J1X 1:J1Y 2:J2X 3:J2Y */
    uint16_t deadband;    /* 默认 120（ADC 计数） */
    uint8_t  invert_mask; /* bit0..3 → 四轴反转 */
    uint8_t  reserved;
    uint32_t crc32;       /* 覆盖 magic..reserved */
} rc_joy_cal_t;           /* 约 36 B ≪ 128 */
```

| 事件 | 行为 |
|------|------|
| 上电 | `nvs_get_blob` → 校验 magic/version/crc → 应用到 joystick 运行时 cfg |
| 校验失败 / 无键 | 固件默认；日志 `rc: joy cal default` |
| 校准完成 / 改死区或反转 | 写回 blob |
| 「恢复默认校准」 | 写默认包或删键后加载默认 |

---

## 5. UI 模式与输入映射

### 5.1 模式

| 模式 | 进入 | 退出 | DRIVE |
|------|------|------|-------|
| **HOME** | 上电默认 | — | 按校准后 cmd 发送 |
| **MENU** | 长按 JS2 ≥ 1.5 s | 根页 BACK / 退出项 | **禁发**（发 0,0 或停发） |
| **CAL_WIZARD** | 菜单「摇杆校准」 | 完成 / 取消 | **禁发** |

### 5.2 HOME / MENU 按键

| 输入 | HOME | MENU |
|------|------|------|
| JS1 Y↑ / Y↓ | （控制） | `MENU_EVT_UP` / `DOWN` |
| JS1 X← / X→ | （控制） | 调 `PARAM`：`LEFT` / `RIGHT` |
| JS1 键短按 | （预留，阶段 A 不用） | `MENU_EVT_ENTER` |
| JS2 键短按 | （预留） | `MENU_EVT_BACK` |
| JS2 键长按 ≥1.5 s | 进 MENU | 忽略或仍作 BACK（实现任选，文档约定：**仅 HOME 触发进入**） |

摇杆导航需：**死区门限 + 边沿触发**（推过阈值产生一次事件，回中后才能再触发），避免连跳。

### 5.3 校准向导内输入

导航映射关闭。约定：

| 步骤 | 用户动作 | 确认 |
|------|----------|------|
| 1 回中 | 双杆松手回中，保持约 0.5–1 s | JS1 键：采样 center |
| 2 极限 | 两杆分别推满左/右/上/下（十字，勿画圆） | 运行时跟踪各轴 min/max；推够后 JS1 键确认 |
| 3 确认 | 屏显摘要 | JS1 键写入 NVS；JS2 键取消不写 |

屏上对「尚未达到合理行程」的轴高亮提示（例如相对默认中心偏移不足阈值）。

---

## 6. 主屏布局（HOME）

240×135 横屏：

```
┌──────────────────────────────────────┐
│ BAT xxxxmV          LINK ●/○         │  ← 顶栏
│                                      │
│   ┌────┐              ┌────┐         │
│   │  + │   JS1        │  + │   JS2   │  ← 双十字，点=归一化 cmd
│   └────┘              └────┘         │
│   T:±xxxx S:±xxxx                    │  ← 可选小字（JS1）
└──────────────────────────────────────┘
```

- 十字中心 = 死区中心；点坐标由 `x_cmd/y_cmd` 线性映射到准星框
- 刷新：与控制周期同级或略慢（如 10–20 Hz），避免 SPI 抢带宽

通道条 **不** 放主屏；见菜单「通道监视」。

---

## 7. 菜单树

```
设置
 ├─ 摇杆校准…          ACTION → CAL_WIZARD
 ├─ 通道监视           子页/自定义绘：四轴 raw + cmd 条
 ├─ 死区               PARAM（get/set/step），默认 120
 ├─ 轴反转             子菜单
 │    ├─ J1X
 │    ├─ J1Y
 │    ├─ J2X
 │    └─ J2Y           各为开关式 PARAM 或 ACTION 翻转 bit
 ├─ 恢复默认校准       DANGER + 二次确认
 └─ 关于               LABEL：产品名 / 版本
```

根页 `on_root_back`：退出 MENU → HOME，恢复 DRIVE。

复用 `components/menu`：`MENU_ITEM_ACTION` / `SUBMENU` / `PARAM` / `MENU_ITEM_F_DANGER`。

---

## 8. 软件分层（建议落点）

| 模块 | 路径 | 职责 |
|------|------|------|
| 采样 + 映射 | `projects/rc-controller/board/joystick.*` | raw；应用运行时 `axis_cfg`；`sample_drive` |
| 校准状态机 | `projects/rc-controller/source/joy_cal.*` | NVS load/save；向导 min/max 数据结构 |
| NVS 适配 | `source/joy_cal.*` | `joy_cal_t` load/save/default/crc |
| 菜单 + UI 状态机 | `projects/rc-controller/source/rc_ui.*` | page/item 表、HOME/MENU/CAL 模式、输入适配 |
| 主屏绘制 | `board/lcd_panel.*` + `source/rc_lcd_cfg.h` | 双十字 + 顶栏 + 菜单/校准帧 |
| 应用编排 | `main/app.c` | 周期 tick；DRIVE 禁发与 proto 发送 |

依赖方向：`app` → menu / joy_cal / joystick / lcd；**menu 组件不依赖 board**。

---

## 9. 安全与边界

1. **MENU / CAL 期间禁止有效 DRIVE**（推荐周期性发 throttle=0, steer=0，或停发并依赖小车超时 failsafe——实现时与 `proto_client` 行为对齐，优先明确「发零」）。
2. 校准未完成退出 → 不覆盖 NVS。
3. 长按进菜单仅在 HOME；避免菜单内再触发进入。
4. `invert` / `deadband` 变更立即作用于映射，并尽快落盘（改完失焦或显式「保存」——阶段 A 推荐 **改完即写 NVS**，简单可靠）。

---

## 10. 实现分期

| 迭代 | 内容 | 状态 |
|------|------|------|
| **A1** | 运行时 cfg + NVS load/save + 默认回退；映射改用 cal 表 | 完成 |
| **A2** | HOME 双十字 + 顶栏 | 完成 |
| **A3** | 长按进 MENU + 菜单树 + 禁发 | 完成 |
| **A4** | 校准向导 + 通道监视 + 死区/反转/恢复默认 | 完成 |
| **B1** | 校准四向最小行程门槛（每侧 ≥600 ADC） | 完成 |

模块落点更新：`source/joy_cal.*`、`source/rc_ui.*`、`source/rc_lcd_cfg.h`（产品独有）；`main/` 仅薄入口。

---

## 11. 已拍板决策摘要

| # | 决策 | 结论 |
|---|------|------|
| 1 | 阶段范围 | A：校准 + NVS + 主屏十字 + 死区/反转；js2 映射占位 |
| 2 | 菜单导航 | JS1 摇杆导航/调参；JS1 键确认；JS2 键返回 |
| 3 | 校准流程 | 双杆一起：回中 → 极限 → 确认 |
| 4 | 存储 | `cal/joy` 单 blob + CRC；可恢复默认 |
| 5 | 主屏 | 左右双十字 + 顶栏 |
| 6 | 进菜单 / 安全 | 长按 JS2 ≥1.5s；菜单与校准禁发 DRIVE |
| 7 | LCD | `RC_LCD_FLIP_UD=1`，`RC_LCD_BGR=0`，深蓝背景 |

---

## 12. 开放项 → 阶段 B 候选

- js2 `aux` 映射到具体协议通道  
- EMA 滤波（固定系数或菜单可调）  
- 校准向导：四向最小行程门槛后才允许确认  
- 禁发策略细化（已实现发零；车端超时 failsafe 属车侧）

## 13. 阶段 B

| 项 | 结论 |
|----|------|
| B1 校准门槛 | 每轴相对回中点，两侧均需 ≥600 ADC；未达标时 Cal 2/3 显示 `reach n/4`，JS1 不能进入确认/保存 |

其余候选仍见 §12。
