# TM4C123 蓝牙二进制协议（UART0）

> 固件实现：`Common/src/proto.c`、`Common/inc/proto.h`  
> PC 上位机：`tools/proto_client/`（PySide6 + pyqtgraph）  
> 物理接口：**UART0 / HC-05**，115200 8N1，小端序。

---

## 1. 串口分工

| 接口 | 引脚 | 用途 |
|------|------|------|
| **UART0** | PA0 RX / PA1 TX | 蓝牙二进制协议（本文件） |
| **UART7** | PE0 RX / PE1 TX | 开发日志、`cmd` 命令行 |

上位机请连接 **蓝牙虚拟 COM**，不要使用 UART7。

---

## 2. 帧格式

```text
[SOF0][SOF1][VER][FLAGS][LEN_L][LEN_H][CMD_L][CMD_H][SEQ][PAYLOAD...][CRC_L][CRC_H]
  0x54   0x4D  0x02   u8      u16 LE          u16 LE        u8     N bytes      u16 LE
```

| 字段 | 说明 |
|------|------|
| SOF | 固定 `0x54 0x4D`（"TM"） |
| VER | 协议版本，当前 `0x02` |
| FLAGS | bit0=设备→主机；bit1=NAK；bit2=主动推送（UNSOLICITED） |
| LEN | Payload 长度 |
| CMD | 命令 ID；应答 CMD \| `0x8000` |
| SEQ | 序号（主机发送，设备原样回显） |
| CRC | CRC16-CCITT-FALSE，覆盖 `VER`~Payload 末字节 |

**CRC16-CCITT-FALSE**：初值 `0xFFFF`，多项式 `0x1021`，无输入/输出反转。

---

## 3. 命令一览

| CMD | 值 | 方向 | 说明 |
|-----|-----|------|------|
| HELLO | `0x0001` | 主机→设备 | 握手，返回版本/能力 |
| PING | `0x0002` | 主机→设备 | 返回 uptime ms |
| GET_TELEMETRY | `0x0010` | 主机→设备 | 单次快照 |
| SUBSCRIBE | `0x0011` | 主机→设备 | 订阅推送通道 |
| UNSUBSCRIBE | `0x0012` | 主机→设备 | 取消订阅 |
| PARAM_LIST | `0x0020` | 主机→设备 | 参数表 |
| PARAM_READ | `0x0021` | 主机→设备 | 读 NVS 参数 |
| PARAM_WRITE | `0x0022` | 主机→设备 | 写 NVS 参数 |
| DRIVE | `0x0030` | 主机→设备 | 遥控（速度环差速） |
| DRIVE_STOP | `0x0031` | 主机→设备 | 停止遥控 |
| **SET_SPEED** | **`0x0032`** | 主机→设备 | **设定轮速目标（RPM）** |
| **SPEED_STOP** | **`0x0033`** | 主机→设备 | **停止速度环** |
| SET_ANGLE | `0x0034` | 主机→设备 | 角度环 |
| ANGLE_STOP | `0x0035` | 主机→设备 | 停止角度环 |
| CALIB_YAW | `0x0036` | 主机→设备 | 航向标定 |
| SET_DISTANCE | `0x0037` | 主机→设备 | 距离环 |
| DISTANCE_STOP | `0x0038` | 主机→设备 | 停止距离环 |
| **SET_LINE_FOLLOW** | **`0x0039`** | 主机→设备 | **启动循迹环** |
| **LINE_FOLLOW_STOP** | **`0x003A`** | 主机→设备 | **停止循迹环** |
| **CAM_SERVO_CENTER** | **`0x0040`** | 主机→设备 | 云台回中（SPI CTRL） |
| **CAM_SERVO_SET_ANGLE** | **`0x0041`** | 主机→设备 | `[ch u8][deg_x100 i16]` |
| **CAM_SERVO_NUDGE** | **`0x0042`** | 主机→设备 | `[ch u8][delta_x100 i16]` |
| **CAM_DETECT_ENABLE** | **`0x0043`** | 主机→设备 | `[on u8]` |
| **GET_CAM_SNAPSHOT** | **`0x0044`** | 主机→设备 | link+detect+servo 快照 |
| **GET_CAM_NET** | **`0x0045`** | 主机→设备 | 图传入口 IP/端口/path_id |
| TELEMETRY_PUSH | `0x8011` | 设备→主机 | 订阅推送载体 |

### 3.1 HELLO 应答 Payload

| 偏移 | 类型 | 说明 |
|------|------|------|
| 0 | u8 | 协议版本 |
| 1 | char[16] | 固件版本字符串 |
| 17 | u32 | hw_rev |
| 21 | u32 | caps 位图 |

**caps 位**：

| bit | 宏 | 说明 |
|-----|-----|------|
| 0 | TELEMETRY | 支持 GET_TELEMETRY |
| 1 | PARAM_RW | 支持参数读写 |
| 2 | SUBSCRIBE | 支持订阅推送 |
| 3 | DRIVE | 支持遥控 |
| 4 | SPEED_LOOP | 支持速度环 SET_SPEED |
| 5 | ANGLE_LOOP | 支持角度环 |
| 6 | YAW_CALIB | 支持航向标定 |
| 7 | DISTANCE_LOOP | 支持距离环 |
| 8 | **LINE_FOLLOW** | **支持循迹环 SET_LINE_FOLLOW** |
| 9 | **CAMERA** | **支持相机 SPI（舵机/检测/图传入口）** |

---

## 4. 速度环（上位机调试）

固件在 `app_tmr` 20 ms 周期运行四轮速度 PID（`chassis_tick`）。**推荐通过上位机 `proto_client` 的「速度调试」页** 订阅 RPM 曲线、写 PID、下发目标转速。

### 4.1 SET_SPEED（0x0032）

Payload 首字节为 **format**：

| format | 长度 | Payload 布局 |
|--------|------|----------------|
| **0** | 6 B | `[0][motor_id u8][rpm i32 LE]`，`motor_id` = 1..4 |
| **1** | 9 B | `[1][left_rpm i32 LE][right_rpm i32 LE]`（M1/M3 左，M2/M4 右） |
| **2** | 17 B | `[2][M1 i32][M2 i32][M3 i32][M4 i32]` 各 LE |

- RPM 有符号，正=前进（经 NVS `motor_dir` 极性修正后输出）。
- 成功后设备进入速度环激活态；会覆盖当前 DRIVE 遥控设定。
- 目标经 `spd_limit.max_rpm` 与 `max_accel_rpm_s` 限幅/斜坡。

**示例（M1 = 100 RPM）**：

```text
format=0: 00 01 64 00 00 00
```

**示例（左 80 / 右 120 RPM）**：

```text
format=1: 01 50 00 00 00 78 00 00 00
```

### 4.2 SPEED_STOP（0x0033）

无 Payload。停止四轮输出、清零 PID 积分与目标。

### 4.3 DRIVE 与速度环关系

`DRIVE` 仍可用：内部转换为左右轮 RPM 目标后走 **同一速度 PID**。500 ms 无新 DRIVE 帧则自动停车。  
`SET_SPEED` 与 `DRIVE` 互斥：发 `SET_SPEED` 会清除 DRIVE 会话。

### 4.4 SET_LINE_FOLLOW（0x0039）

| format | 长度 | Payload |
|--------|------|---------|
| **0** | 1 B | `[0]` — 使用 NVS `line_base_rpm`（默认 80） |
| **1** | 5 B | `[1][base_rpm i32 LE]` — 本次基准转速覆盖 |

外环：6 路 ADC → 加权偏差 → `pid_line` → 左右差速；内环仍为速度 PID。  
丢线后先沿**上次偏差方向短时搜索**（约 1 s），仍全白则停车退出循迹模式。  
默认 `pid_line`：Kp=8, Ki=0, Kd=0.25（偏快纠偏）；可用 PARAM 覆盖。

### 4.5 LINE_FOLLOW_STOP（0x003A）

无 Payload。停止循迹并清零电机输出。

---

## 5. 订阅与 RPM 推送

### 5.1 SUBSCRIBE Payload

| 偏移 | 类型 | 说明 |
|------|------|------|
| 0 | u32 | channel mask（LE） |
| 4 | u8 | 姿态 Hz（0=默认 50） |
| 5 | u8 | 编码器计数 Hz（默认 20） |
| 6 | u8 | 循迹 ADC Hz（默认 20） |
| 7 | u8 | 超声波 Hz（默认 10） |
| 8 | u8 | **可选** 电机 RPM Hz（默认 20） |
| 9 | u8 | **可选** 角度环 Hz |
| 10 | u8 | **可选** 距离环 Hz |
| 11 | u8 | **可选** 循迹环 Hz（默认 10） |
| 12 | u8 | **可选** 相机检测 Hz（默认 10） |
| 13 | u8 | **可选** 云台遥测 Hz（默认 10） |

**常驻推送（固件启动后自动，无需订阅）**：

| 通道 | 默认 Hz | push `ch` |
|------|---------|-----------|
| 姿态 | 10 | 1 |
| 编码器计数 | 10 | 2 |

**按需订阅（SUBSCRIBE mask 可选位）**：

| bit | 通道 | push `ch` 字段 |
|-----|------|----------------|
| 0 | 电量 | 0 |
| 3 | 循迹 ADC | 3 |
| 4 | 超声波 | 4 |
| 5 | 电机 RPM | 5 |
| 6 | 角度环 | 6 |
| 7 | 距离环 | 7 |
| **8** | **循迹环** | **8** |
| **9** | **相机检测** | **9** |
| **10** | **云台遥测** | **10** |

`SUBSCRIBE` 中 **bit1/bit2** 仅用于调整常驻通道 Hz；`UNSUBSCRIBE 0xFFFFFFFF` 只清除可选通道，不停姿态/编码器。

速度调试：在仪表盘勾选 **电机 RPM** 后 `SUBSCRIBE`；勿一次订阅全部可选通道（HC-05 半双工易拥塞）。

### 5.2 电池推送 Payload（ch = 0）

| 偏移 | 类型 | 说明 |
|------|------|------|
| 0 | u8 | `0` |
| 1 | u32 | uptime ms |
| 5 | u16 | batt_mv |
| 7 | u8 | batt_pct |

### 5.3 循迹推送 Payload（ch = 3）

| 偏移 | 类型 | 说明 |
|------|------|------|
| 0 | u8 | `3` |
| 1 | u32 | uptime ms |
| 5 | u8 | **位掩码**：bit0~bit5 = LINE1~LINE6；bit i = 1 表示检测到黑线 |
| 6 | u16×6 | **原始 ADC**（12-bit，LE）；L1…L6 |

**物理通道（车头朝前，左 → 右）**：

| 序号 | 引脚 | ADC | 说明 |
|------|------|-----|------|
| L1 / bit0 | **PD3** | AIN4 | 最左 |
| L2 / bit1 | **PD2** | AIN5 | |
| L3 / bit2 | **PD1** | AIN6 | **中线（左）** |
| L4 / bit3 | **PD0** | AIN7 | **中线（右）** |
| L5 / bit4 | **PE5** | AIN8 | |
| L6 / bit5 | **PE4** | AIN9 | 最右 |

偏差权重：`[-5, -3, -1, +1, +3, +5]`；L3+L4 同时压线时 error≈0。

上位机：**仪表盘**用位掩码显示 0/1；**曲线页**画原始 ADC。

两车型均为 **6 路**。默认 **高电平为黑**（ADC ≥ threshold）；可通过 `LINE_POLARITY` 改为低电平为黑。

### 5.3.1 循迹环推送 Payload（ch = 8）

| 偏移 | 类型 | 说明 |
|------|------|------|
| 0 | u8 | `8` |
| 1 | u32 | uptime ms |
| 5 | i16 | 横向偏差 ×100（约 −500…+500，对应 −5…+5） |
| 7 | u8 | 检测位掩码（同循迹 ADC） |
| 8 | u8 | 状态：0=IDLE，1=TRACK，2=LOST |
| 9 | i32 | 左组目标 RPM |
| 13 | i32 | 右组目标 RPM |
| 17 | i32 | 转向分量 RPM（差速） |

上位机「循迹 / 调试」页：上图 ADC，下图 error / L/R / turn；右侧可启停循迹并编辑 `pid_line`。

### 5.4 MOTOR_RPM 推送 Payload（ch = 5）

| 偏移 | 类型 | 说明 |
|------|------|------|
| 0 | u8 | `5` |
| 1 | u32 | uptime ms |
| 5 | i32 | M1 RPM |
| 9 | i32 | M2 RPM |
| 13 | i32 | M3 RPM |
| 17 | i32 | M4 RPM |

推送帧 CMD = `TELEMETRY_PUSH (0x8011)`，FLAGS 含 UNSOLICITED。

---

## 6. 参数读写（速度 PID 整定）

通过 `PARAM_READ` / `PARAM_WRITE` 读写 NVS。Payload：`[param_id u16 LE][blob...]`（写）。

| param_id | 名称 | 大小 | 布局 |
|----------|------|------|------|
| 5 | PID_SPEED | 12 | `f32 kp, ki, kd` LE |
| 6 | PID_LINE | 12 | 循迹外环 PID |
| 7 | SPD_LIMIT | 8 | `f32 max_rpm, max_accel_rpm_s` LE |
| 8 | KINEMATICS | 20 | 见 `nvs.h` |
| 11 | LINE_THRESHOLD | 12 | `u16 th[6]` LE，默认全 2048 |
| 19 | LINE_POLARITY | 1 | `u8`：0=低电平为黑，**1=高电平为黑（默认）** |
| 20 | LINE_BASE_RPM | 4 | `f32` 循迹基准 RPM，默认 80 |

**速度环整定流程（上位机）**：

1. 连接蓝牙 COM，`HELLO` 确认 `caps` bit4。
2. `SUBSCRIBE` mask \| `0x20`，打开 **速度曲线** 页观察 M1~M4 反馈。
3. `PARAM_READ` id=5 读取当前 `pid_speed`；在界面修改 Kp/Ki/Kd 后 `PARAM_WRITE` 写回。
4. 用 **SET_SPEED format=0** 对单轮阶跃（如 0→100 RPM），看曲线超调/调节时间。
5. 满意后可选 `PARAM_WRITE` 保存；设备重启仍从 NVS 加载。

默认出厂：`Kp=1, Ki=0, Kd=0`，`max_rpm=300`，需实机整定。

---

## 7. GET_TELEMETRY 快照布局

（与推送独立，单次查询）

| 偏移 | 内容 |
|------|------|
| 0 | batt_mv u16 |
| 2 | batt_pct u8 |
| 3 | roll i16 |
| 5 | pitch i16 |
| 7 | yaw i16 |
| 9 | enc[0..3] u32 ×4（计数，非 RPM） |
| 25 | line_det u8 | 位掩码：bit0~bit5 = LINE1~LINE6；bit i = 1 表示检测到线 |
| 26 | uptime u32 |
| 30 | ultrasonic mm u16 |

完整 ADC 不在协议上传；阈值比较在固件内完成（`cfg_line_threshold`）。

实时 RPM 请用 **订阅 ch=5**，不要对编码器计数自行差分（与固件滤波不一致）。

---

## 8. 错误码（NAK Payload 1 字节）

| 值 | 含义 |
|----|------|
| 0x02 | 未知命令 |
| 0x03 | 长度错误 |
| 0x04 | 参数 ID 无效 |
| 0x05 | 只读 |
| 0x06 | 参数值非法 |
| 0x07 | NVS 写失败 |
| 0x08 | 忙 |
| 0x09 | 不支持（如无电机能力） |

---

## 9. proto_client 上位机

路径：**`tools/proto_client/`**（源码已纳入 Git；`tools/` 下 ARM 工具链仍本地安装）。

```powershell
cd tools\proto_client
.\run.cmd
```

| Tab | 功能 |
|-----|------|
| 仪表盘 | 电池 / 姿态 / 编码器 / 循迹 / 超声波 |
| 姿态 / 循迹 | 实时曲线 |
| **速度调试** | SET_SPEED、RPM 曲线、pid_speed / spd_limit 联动 |
| 遥控 | DRIVE |
| **相机 / 云台** | CAM_SERVO_*、检测推送、GET_CAM_NET + HTTP JPEG 预览 |

CLI 示例：

```text
python cli.py COM7 cam-center
python cli.py COM7 cam-snapshot
python cli.py COM7 cam-net
```

---

## 10. 相机命令载荷

### 10.1 GET_CAM_SNAPSHOT 应答（34 B）

| 偏移 | 字段 |
|------|------|
| 0 | link u8 |
| 1 | peer_role u8 |
| 2 | detect_valid u8 |
| 3 | count u8 |
| 4 | best_index u8 |
| 5..6 | frame_w u16 |
| 7..8 | frame_h u16 |
| 9..24 | box0+box1（各 8B） |
| 25 | servo_valid u8 |
| 26..27 | pan_deg_x100 i16 |
| 28..29 | tilt_deg_x100 i16 |
| 30..31 | pan_pulse_us u16 |
| 32..33 | tilt_pulse_us u16 |

### 10.2 GET_CAM_NET 应答（10 B）

| 偏移 | 字段 |
|------|------|
| 0..3 | ipv4 u32 LE |
| 4..5 | http_port u16 |
| 6 | wifi_mode u8 |
| 7 | flags u8 |
| 8 | stream_path_id u8 |
| 9 | valid u8 |

上位机组 URL：`http://a.b.c.d:port/api/camera/stream.mjpg`（path_id=0）。图像走 WiFi，不经蓝牙。

### 10.3 推送 ch=9 / ch=10

与固件 `proto_push_cam_detect` / `proto_push_cam_servo` 一致：`[ch][uptime u32][body…]`。
| 参数面板 | schema.json 全部 NVS 参数 |

分层架构见 [`tools/proto_client/README.md`](../tools/proto_client/README.md)。

---

## 10. 相关文件

| 路径 | 说明 |
|------|------|
| `Common/src/proto.c` | 协议实现 |
| `Common/src/chassis.c` | 速度环 |
| `Common/inc/nvs.h` | 参数 ID 与结构体 |
| `docs/closed-loop-plan.md` | 闭环分阶段计划 |

---

## 11. 修订记录

| 日期 | 说明 |
|------|------|
| 2026-07-12 | 初版：帧格式、SET_SPEED/SPEED_STOP、RPM 推送、上位机速度调试 |
