# TM4C123 遥控器（rc-controller）

基于 TM4C123GH6PM + FreeRTOS 的手持遥控器，经 **UART0 蓝牙**向小车发送 `docs/bluetooth-protocol.md` 协议帧。

## 引脚分配（当前板）

配置源：`projects/rc-controller/.syscfg/`（改后重新编译）

板级目录 `board/`：

| 文件 | 说明 |
|------|------|
| `board.c` / `board.h` | 自动生成：UART/I2C/SPI/ADC 初始化 |
| `joystick.c` | 双摇杆 ADC + 按键 |
| `lcd_panel.c` | ST7789 1.14" 显示 |
| `nrf24.c` | NRF24 GPIO 占位（SPI 驱动待接） |

`main/` 仅保留应用入口（`app.c`、`main.c` 等），不含小车 motor/encoder/line 模块。

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

操纵杆1：Y→throttle，X→steer。

## 冒烟测试（UART7 @ 115200）

上电后 `app.c` 内 `RC_BOOT_SMOKE` 块自动跑一轮外设检测（验证通过后改 `RC_BOOT_SMOKE` 为 0 或删除该块）。日志示例：

```
[INFO] smoke: start product=rc-controller
[INFO] smoke: i2c 0x2C,0x69
[INFO] smoke: imu ok
[INFO] smoke: mag ok
[INFO] smoke: joy j1=2048,1850
[INFO] smoke: bat 3720mV
[INFO] smoke: led green
[INFO] smoke: spi ok
[INFO] smoke: proto link up
[INFO] smoke: done pass=8/8
```

无需交互命令；失败项会打 `[WARN] smoke: ...`。

## 显示（ST7789 1.14" 240×135）

- 驱动：`cbb/st7789` + `board/lcd_panel.c`
- 横屏：标题、版本、摇杆 T/S、电池 mV、蓝牙链路状态
- 上电约 1s 内应看到深蓝底 + 白字 **RC Controller**

## 待实现

- NRF24 2.4G 驱动
- 操纵杆2 功能映射
