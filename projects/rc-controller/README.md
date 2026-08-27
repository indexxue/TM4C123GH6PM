# TM4C123 遥控器（rc-controller）

基于 TM4C123GH6PM + FreeRTOS 的手持遥控器，经 **UART0 蓝牙**向小车发送 `docs/bluetooth-protocol.md` 协议帧。

**使用说明（操作/校准/菜单）** → [docs/rc-controller-usage.md](../../docs/rc-controller-usage.md)  
**设计文档** → [docs/rc-joystick-menu-design.md](../../docs/rc-joystick-menu-design.md)

## 功能摘要（阶段 A + B1 + R1 + R2）

| 功能 | 说明 |
|------|------|
| 主屏 HOME | 双十字 + 电池 + LINK + **当前模型名**（顶栏） |
| **手动建链** | 上电 **LINK --**；**JS1 短按** 发起 HELLO → LINK OK |
| **多模型** | 出厂 Stick / Line / Tilt；NVS `rc/models`；**JS2 短按**循环切换 |
| **Tilt 模型** | JS2 切到 Tilt；水平持握 0.5 s 零点；JS1 进 DRIVE 后倾斜控车 |
| **JS2 长按** | 目标设备 overlay（R3 占位） |
| **Settings** | HOME 下 **JS1+JS2 同时短按**；含 Switch Model / Subscribe |
| 摇杆校准 | 回中 → 极限（每侧 ≥600 ADC）→ 确认写 NVS |
| NVS | `cal/joy` blob：min/center/max ×4 轴 + deadband + invert |
| 通道监视 | 四轴 raw/cmd |
| 死区 / 反转 | 菜单可调，改完即落盘 |

## 引脚分配（当前板）

配置源：`projects/rc-controller/.syscfg/`（改后重新编译）

板级目录 `board/`：

| 文件 | 说明 |
|------|------|
| `board.c` / `board.h` | 自动生成：UART/I2C/SPI/ADC 初始化 |
| `joystick.c` | 双摇杆 ADC + 按键 + 校准表映射 |
| `lcd_panel.c` | ST7789 1.14" 主屏/菜单/校准 UI |
| `nrf24.c` | NRF24 GPIO 占位（SPI 驱动待接） |

产品独有逻辑在 `source/`（勿放进 Common）：

| 文件 | 说明 |
|------|------|
| `joy_cal.*` | 摇杆校准 NVS |
| `rc_mixer.*` | 摇杆/IMU 倾斜 → throttle/steer |
| `rc_model.*` | 多模型 NVS（最多 8 套） |
| `rc_input.*` | 摇杆采样 + 按键边沿 |
| `rc_link.*` | 手动建链 + proto 封装 |
| `screen/screen_home.*` | HOME 屏绘制 |
| `screen/screen_target.*` | Target overlay（R3 占位） |
| `rc_ui.*` | DRIVE/菜单/校准 UI 状态机 |
| `rc_lcd_cfg.h` | 屏向翻转 / BGR·RGB / 主题色宏 |

共享菜单引擎：`components/menu/`（`build.ps1` 已编入本工程）。

`main/` 仅保留薄入口（`app.c`、`main.c`、启动与 hooks）。

屏向/颜色：改 `source/rc_lcd_cfg.h` 里 `RC_LCD_FLIP_UD`、`RC_LCD_BGR`、`RC_LCD_COLOR_*` 后重编。

| 功能 | 引脚 |
|------|------|
| 蓝牙 UART0 | PA0 RX / PA1 TX |
| 调试 UART7 | PE0 RX / PE1 TX |
| I2C0 软件 | PB2 SCL / PB3 SDA → MPU6050@0x69、QMC5883P@0x2C |
| WS2812 | PC3 |
| SPI0 共享 | PA2 CLK、PA4 MOSI、PA5 MISO |
| ST7789 | CS=PA3、RST=PF0、DC=PF1、BL=PF2 |
| NRF24 | CS=PA6、CE=PB0、IRQ=PB1 |
| 操纵杆1 | ADC PD0(X)/PD1(Y)，按键 PC4 |
| 操纵杆2 | ADC PD2(X)/PD3(Y)，按键 PB6 |
| 电池 | PE3 ADC1 AIN0（100K/100K 分压） |

完整表见编译生成的 `gpio-allocation.md`。

## 构建 / 烧录

```powershell
.\build.cmd rc-controller
.\flash-jlink.cmd -CarProject rc-controller
```

## 协议

- 主机：`Common/src/proto_client.c`（发 `HELLO` / `DRIVE`）
- 小车：`Common/src/proto.c`（收 `DRIVE`）

操纵杆1：Y→throttle，X→steer。操纵杆2：阶段 A 仅占位（监视/校准），不发 aux。

## 调试日志（UART7 @ 115200）

上电正常应看到类似：

```
[INFO] rc: tasks started
[INFO] joy_cal: loaded from nvs
[INFO] lcd: st7789 240x135 ready
[INFO] rc: ui ready mode=HOME (hold JS2 for menu)
```

进入菜单 / 校准 / 保存校准时会有 `rc_ui:` 前缀日志。

## 显示（ST7789 1.14" 240×135）

- 驱动：`cbb/st7789` + `board/lcd_panel.c`
- **HOME**：顶栏 BAT xx% + LINK，左右 JS1/JS2 十字准星，底栏进菜单提示
- **Settings**：列表菜单（校准、监视、死区、反转、恢复默认、关于）
- **Calibrate**：三步向导 + 双十字实时反馈

操作细节见 [docs/rc-controller-usage.md](../../docs/rc-controller-usage.md)。

## 待实现

- NRF24 2.4G 驱动
- 操纵杆2 业务通道映射（aux / 云台等）
- Expo / Dual Rate / EPA
