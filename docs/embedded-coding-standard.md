# TM4C123 嵌入式编码规范（依赖分层）

与 ESP32-S3 参考仓库对齐的依赖方向；新增模块前请先确认所属层级。

## 分层

```
projects/<car>/main   →  common, bsp_driver, board（full profile）
common                →  bsp_driver, board（full profile）, cbb
board/（syscfg 生成）  →  bsp_driver / TivaWare DriverLib
cbb/                  →  bsp_driver 或 TivaWare（不依赖 common）
bsp_driver/           →  TivaWare DriverLib only
```

## 目录

| 路径 | 职责 |
|------|------|
| `bsp_driver/` | MCU 外设薄封装，**统一** `bsp_<外设>.{h,c}`（见 §BSP 驱动命名） |
| `cbb/` | 芯片协议驱动（ws2812b、mpu6050、tb6612…），回调注入 |
| `Common/` | 跨产品服务：log、nvs、cmd、ota、device_profile、start |
| `projects/<car>/board/` | syscfg 生成的 motor/encoder/line/board |
| `projects/<car>/main/` | 薄应用：main.c、app.c、FreeRTOS 钩子 |

## BSP 驱动命名（`bsp_driver/`）

| 规则 | 说明 |
|------|------|
| 文件名 | `inc/bsp_<外设>.h` 与 `src/bsp_<外设>.c` 成对出现 |
| 前缀 | 一律 `bsp_`，避免与 `include/gpio.h`、`driverlib/*.h` 等同名冲突 |
| 外设名 | 简短 MCU 模块名：`gpio`、`uart`、`i2c`、`spi`、`adc`、`dac`、`timer`、`qei`、`dma`、`systick`、`sysctl` |
| 禁止 | 裸名 `clock.h`、`uart.h`、`timer_pwm.h`；PWM 归入 `bsp_timer` |
| 头 guard | `BSP_DRIVER_<外设>_H` |
| 公共 API | 函数以 `bsp_<外设>_` 或 `bsp_<动作>_` 前缀（如 `bsp_clock_init` 在 `bsp_sysctl` 模块） |

| 模块 | 文件 | 职责 |
|------|------|------|
| 系统时钟 | `bsp_sysctl` | SysCtl 时钟源与 `bsp_clock_get_hz()` |
| GPIO | `bsp_gpio` | 端口使能、读写 |
| 定时/PWM | `bsp_timer` | Timer PWM init / 占空比 |
| UART | `bsp_uart` | 串口 init / 收发 |
| I2C | `bsp_i2c` | 主机 init / 探测 / 读写 |
| SPI | `bsp_spi` | SSI 主机 init / 收发（TM4C123 硬件为 SSI） |
| ADC | `bsp_adc` | 序列采样 |
| DAC | `bsp_dac` | PWM 软 DAC（本芯片无片上 DAC） |
| QEI | `bsp_qei` | 编码器 |
| DMA | `bsp_dma` | uDMA init |
| 总线锁 | `bsp_bus_lock` | I2C/SPI FreeRTOS mutex |
| 延时 | `bsp_systick` | DWT 微秒 + FreeRTOS 毫秒 |
| 公共 | `bsp_config.h` | `stdbool` 等前置头（仅 BSP 内部） |

## device_profile

- 编译期 `-DDEVICE_PRODUCT_ID=...` 选择产品档案
- `board_mask` 控制 Motor/Encoder/Line/Board_Periph 初始化
- `platform_mask` 控制 log/cmd/ota/led/button 等平台服务
- v1 默认：`car-4wd` + `log-only`（仅 UART7 日志心跳）

## 构建

```powershell
.\build.cmd                              # car-4wd log-only（默认）
.\build.cmd -Profile full                # 全外设
.\build.cmd -CarProject car-2wd -Profile full
.\flash-jlink.cmd
```

## 禁止

- 在 `Common/inc/` 放置 syscfg 生成的 board 头文件
- `cbb` 或 `bsp_driver` 引用 `common` 头文件
- 手改 `projects/*/board/src/{motor,encoder,line,board}.c`
