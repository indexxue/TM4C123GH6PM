# 蓝牙协议层设计（UART0 / HC-05）

> 本文档是 **UART0 蓝牙业务协议** 的单一说明源。  
> 调试串口 **UART7** 继续专供 `log` + `cmd`，禁止与蓝牙口混用。  
> PC 工具实现见 `tools/proto_client/`。

---

## 1. 背景与目标

| 项 | 说明 |
|----|------|
| 物理接口 | HC-05 蓝牙模块，透明串口 |
| MCU 外设 | UART0 @ PA0/PA1，115200 8N1 |
| 服务对象 | **PC 为主**（标定/遥测/调参），**App 为辅**（遥控子集） |
| 协议形态 | **统一二进制帧**；PC 全量命令，App 仅实现子集 |
| 固件模块 | `Common/src/proto.c` |
| PC 工具 | `tools/proto_client/`（PySide6 + pyqtgraph） |

### v1 实现优先级

1. **遥测**（轮询快照 + 订阅推送）
2. **参数读写**（`nvs_param_id_t` 映射）
3. **遥控**（阶段 A：油门 + 转向 + 看门狗急停）

---

## 2. 帧格式

```
[SOF 2B][VER 1B][FLAGS 1B][LEN 2B][CMD 2B][SEQ 1B][PAYLOAD][CRC16 2B]
```

所有多字节整数均为 **小端（LE）**。CRC 覆盖范围：`VER` … `PAYLOAD`（不含 SOF、不含 CRC 本身）。

| 字段 | 长度 | 值 / 说明 |
|------|------|-----------|
| SOF | 2 B | `0x54 0x4D`（"TM"） |
| VER | 1 B | `0x01` |
| FLAGS | 1 B | 见下表 |
| LEN | 2 B | 载荷字节数 |
| CMD | 2 B | 命令码 |
| SEQ | 1 B | 0–255 循环；主动推送不参与匹配 |
| PAYLOAD | LEN | 变长 |
| CRC16 | 2 B | CCITT-FALSE（poly `0x1021`，init `0xFFFF`） |

### FLAGS

| bit | 名称 | 说明 |
|-----|------|------|
| 0 | DIR | `0` = PC→车，`1` = 车→PC |
| 1 | ACK/NAK | 仅响应：`0` = ACK，`1` = NAK |
| 2 | UNSOLICITED | `1` = 主动推送（无对应请求） |
| 3–7 | — | 保留，置 0 |

### 请求-响应约定

- PC 发请求：`DIR=0`，`SEQ` 递增，等待同 `SEQ` 的响应。
- 车端成功响应：`DIR=1`，`ACK`，`CMD = 请求 CMD \| 0x8000`。
- 车端失败响应：`DIR=1`，`NAK`，载荷 `[err_code u8]`。
- **CRC 错误**：静默丢弃，不回 NAK（防噪声风暴）。
- 主动推送：`CMD=TELEMETRY_PUSH`，`UNSOLICITED=1`，**不做 SEQ 匹配**。

---

## 3. 错误码

| 码 | 名称 | 场景 |
|----|------|------|
| `0x02` | UNKNOWN_CMD | opcode 未实现 |
| `0x03` | BAD_LEN | 载荷长度错误 |
| `0x04` | PARAM_ID_INVALID | param_id 越界 |
| `0x05` | PARAM_READ_ONLY | `nvs_param_write_allowed` 拒绝 |
| `0x06` | PARAM_VALUE_INVALID | 数值超范围 |
| `0x07` | NVS_WRITE_FAIL | Flash 写入失败 |
| `0x08` | BUSY | 车端忙 |
| `0x09` | UNSUPPORTED | 能力位未开启 |

---

## 4. 命令表

| CMD | 值 | v1 | 方向 | 说明 |
|-----|-----|-----|------|------|
| HELLO | `0x0001` | ✓ | PC→车 | 空载荷；交换版本与能力 |
| HELLO_RSP | `0x8001` | ✓ | 车→PC | 见 §5.1 |
| PING | `0x0002` | ✓ | PC→车 | 空载荷 |
| PING_RSP | `0x8002` | ✓ | 车→PC | `[uptime_ms u32]` |
| GET_TELEMETRY | `0x0010` | ✓ | PC→车 | 全量快照 |
| TELEMETRY_RSP | `0x8010` | ✓ | 车→PC | 见 §5.2 |
| SUBSCRIBE | `0x0011` | ✓ | PC→车 | 见 §5.3 |
| UNSUBSCRIBE | `0x0012` | ✓ | PC→车 | `[mask u32]` |
| TELEMETRY_PUSH | `0x8011` | ✓ | 车→PC | 主动推送，见 §5.4 |
| PARAM_LIST | `0x0020` | ✓ | PC→车 | 空载荷 |
| PARAM_LIST_RSP | `0x8020` | ✓ | 车→PC | 见 §5.5 |
| PARAM_READ | `0x0021` | ✓ | PC→车 | `[param_id u16]` |
| PARAM_READ_RSP | `0x8021` | ✓ | 车→PC | 原始结构体二进制 |
| PARAM_WRITE | `0x0022` | ✓ | PC→车 | `[param_id u16][value…]` |
| DRIVE | `0x0030` | 阶段 A | PC→车 | `[throttle i16][steer i16]`，-1000…1000 |
| DRIVE_STOP | `0x0031` | 阶段 A | PC→车 | 空载荷，立即停车 |

响应 CMD 统一为：**请求 CMD \| 0x8000**（`TELEMETRY_PUSH` 除外）。

---

## 5. 载荷定义

### 5.1 HELLO_RSP

```
proto_ver   u8
fw_version  char[16]   /* 以 '\0' 结尾 */
hw_rev      u32
caps        u32        /* 能力位图 */
```

**caps 位：**

| bit | 名称 |
|-----|------|
| 0 | TELEMETRY |
| 1 | PARAM_RW |
| 2 | SUBSCRIBE |
| 3 | DRIVE |

### 5.2 GET_TELEMETRY 快照（TELEMETRY_RSP）

```
batt_mv     u16
batt_pct    u8
roll        f32
pitch       f32
yaw         f32
enc[4]      s32
line_adc[6] u16
uptime_ms   u32
```

未就绪字段（如编码器）填 0。

### 5.3 SUBSCRIBE

```
mask        u32     /* 通道位图，见 §5.4 */
hz_att      u8      /* CH_ATTITUDE 频率，0=默认 50 */
hz_enc      u8      /* CH_ENCODER，0=默认 20 */
hz_line     u8      /* CH_LINE_ADC，0=默认 20 */
reserved    u8
```

**通道位：**

| bit | 名称 | 推送 channel_id | 默认载荷 |
|-----|------|-----------------|----------|
| 0 | CH_BATT | — | 仅快照，不可订阅 |
| 1 | CH_ATTITUDE | 1 | f32 roll, pitch, yaw |
| 2 | CH_ENCODER | 2 | s32[4] |
| 3 | CH_LINE_ADC | 3 | u16[6] |

v1 PC 默认订阅：`CH_ATTITUDE @ 50 Hz` + `CH_LINE_ADC @ 20 Hz`。

### 5.4 TELEMETRY_PUSH（主动推送）

```
channel_id  u8
uptime_ms   u32
payload     …       /* 按 channel_id 解析 */
```

### 5.5 PARAM_LIST_RSP

每条 8 字节，重复至帧结束：

```
param_id    u16
flags       u8      /* bit0 = 协议可写 */
size        u8      /* 结构体字节数 */
reserved    u16
```

本地 schema 详见 `tools/proto_client/schema.json`（与 `Common/inc/nvs.h` 对齐）。

### 5.6 参数读写

- 寻址：`param_id` = `nvs_param_id_t`（见 `nvs.h`）。
- 写入来源：`NVS_WRITE_SRC_PROTOCOL`。
- 仅 `nvs_param_write_allowed(id, PROTOCOL)` 为真的项可写。
- 写入成功后内存缓存 `s_cfg` 热更新，无需额外 `cfg_reload`。

---

## 6. 链路管理

| 机制 | 参数 |
|------|------|
| 连接握手 | 连接后 PC 发 `HELLO`，确认 `proto_ver` 与 `caps` |
| 保活 | PC 每 **2 s** 发 `PING` |
| 断线判定 | **10 s** 无 `PING_RSP` → PC 标为断线 |
| 快照轮询 | PC 默认 **5 Hz** `GET_TELEMETRY` |

---

## 7. 遥控（阶段 A）

| 项 | 值 |
|----|-----|
| 模型 | 油门 `throttle` + 转向 `steer`，各 `i16`，范围 -1000…1000 |
| 运动学 | 固件用 `nvs_kinematics_t` 换算四轮 RPM |
| 看门狗 | **500 ms** 未收到 `DRIVE` → 四轮 RPM 置 0 |
| PC 发送频率 | **20 Hz** 重发 `DRIVE`（即使值不变） |
| 急停 | `DRIVE_STOP` 立即清零，不等看门狗 |
| 任务上下文 | `DRIVE` 入队 `app_evt`，在 `app_on_timer` 20 ms 环执行 |

---

## 8. 固件架构

```
Common/inc/proto.h
Common/src/proto.c
  proto_uart_service_start()    /* App_Start() 调用 */
  proto_rx_task()               /* UART0 轮询 + 帧状态机 + 命令分发 */
  proto_telemetry_tick()        /* app_on_timer() 调用：订阅推送 + 驱动看门狗 */

projects/car-4wd/main/app.c
  app_on_timer() → proto_telemetry_tick()
```

| 任务 | 职责 |
|------|------|
| `proto_rx` | 解析帧、同步响应、PARAM 读写 |
| `app_on_timer` | 订阅通道到期推送、`DRIVE` 看门狗 |
| `cmd`（UART7） | 开发调试，**不迁移**到蓝牙口 |

TX 走 `board.h`：`UART_BT_Putc` / `UART_Putc`（UART0）。

---

## 9. PC 工具

路径：`tools/proto_client/`

```
tm_proto.py         # 帧编解码、CRC16、载荷解析
serial_worker.py    # QThread 串口收发
main_window.py      # PySide6 主界面
cli.py              # 无头调试
schema.json         # param_id → 结构体布局
requirements.txt
```

### 蓝牙收发冒烟测试

链路验证分两步：**原始回显**（确认 HC-05 透传）→ **HELLO 协议**（确认 `proto.c`）。

**1. 原始收发（echo）**

UART7 调试口（115200）连接 PC，烧录最新 `app.bin` 后：

```
bt echo on
```

蓝牙串口（UART0 虚拟 COM）执行：

```powershell
cd tools\proto_client
python bt_smoke_test.py COM12 echo
```

应看到 `PASS: echo payload found in reply`。测完在 UART7 发送：

```
bt echo off
```

**2. 车端主动发送（tx）**

UART7：

```
bt tx hello-from-mcu
```

蓝牙串口：

```powershell
python bt_smoke_test.py COM12 tx
```

或先开监听，再在 UART7 发 `bt tx ...`。

**3. 协议 HELLO（proto 模式）**

确保已 `bt echo off`，然后：

```powershell
python bt_smoke_test.py COM12 hello
```

或 `python cli.py COM12 hello`。

### v1 UI 模块

1. **连接栏**：COM 口、连接/断开、HELLO 信息、PING 状态（10 s 超时）
2. **遥测仪表盘**：电量、姿态、编码器、循迹 ADC
3. **实时曲线**：pyqtgraph 订阅通道
4. **参数面板**：基于 `schema.json` 的读写表单

---

## 10. 实现分期

| 阶段 | 内容 |
|------|------|
| **v1a** | `proto.c`：帧解析 + HELLO/PING + PARAM |
| **v1b** | GET_TELEMETRY + SUBSCRIBE/TELEMETRY_PUSH |
| **v1c** | PC 工具 UI（本仓库 `tools/proto_client/`） |
| **v2** | DRIVE + app_evt 队列 + 500 ms 看门狗 |
| **后续** | TLV 批量参数、App 子集、OTA |

---

## 11. 相关文件

| 路径 | 说明 |
|------|------|
| `Common/inc/nvs.h` | 参数 ID 与结构体 |
| `Common/src/nvs.c` | `NVS_WRITE_SRC_PROTOCOL` 写入策略 |
| `projects/car-4wd/board/inc/board.h` | UART0 引脚与 API |
| `bsp_driver/src/bsp_uart.c` | UART 薄封装 |
| `.cursor/rules/project-context.mdc` | 串口分工约束 |
