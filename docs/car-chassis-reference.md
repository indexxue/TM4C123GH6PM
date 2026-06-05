# TM4C123GH6PM 小车底盘开发参考

本文档基于 TM4C123GH6PM 主控芯片，规划 4 轴 MG513 电机 + 多传感器小车的引脚与外设分配。

---

## 外设需求概览

| 模块 | 接口类型 | TM4C123 资源 | 数量 |
|---|---|---|---|
| 电机驱动 | PWM 输出 | Timer PWM 通道 | 4 路 |
| 方向控制 | GPIO 输出 | GPIO | 8 路 |
| 编码器输入 | QEI / GPIO 捕获 | QEI ×2 + GPIO ×6 | 8 路 |
| 蓝牙/WiFi | UART | UART0/1/2/3/4/5/6/7 | 1 路 |
| 超声波 (HC-SR04) | GPIO (Trig + Echo) | GPIO + Timer Capture | 1-2 路 |
| IMU (MPU6050) | I2C | I2C0/1/2/3 | 1 路 |
| 循迹传感器 | GPIO 输入 / ADC | GPIO / ADC0 | 4-8 路 |
| OLED 显示 | I2C 或 SPI | I2C / SSI (SPI) | 1 路 |
| 电池电压检测 | ADC | ADC0 通道 | 1 路 |
| 蜂鸣器 | GPIO (PWM) | GPIO / Timer PWM | 1 路 |

---

## TM4C123GH6PM 外设资源

### 定时器 (Timer) — 电机 PWM 驱动

6 个通用定时器 (Timer0 ~ Timer5)，每个定时器有 A/B 两个子通道。
共 12 路 PWM 输出通道，4 路电机绰绰有余。

**TivaWare API：**
```c
// 配置 Timer0A 为 PWM 输出
SysCtlPeripheralEnable(SYSCTL_PERIPH_TIMER0);
TimerConfigure(TIMER0_BASE, TIMER_CFG_SPLIT_PAIR | TIMER_CFG_A_PWM);
TimerLoadSet(TIMER0_BASE, TIMER_A, SysCtlClockGet() / 10000);  // 10kHz PWM
TimerMatchSet(TIMER0_BASE, TIMER_A, duty_cycle);               // 占空比
TimerEnable(TIMER0_BASE, TIMER_A);
```

**参考示例：** `examples/peripherals/timer/pwm.c`

### QEI (正交编码器接口) — 编码器测速

TM4C123GH6PM 有 **2 组 QEI 模块**，可硬件解码正交编码器信号。

| 模块 | PhA 引脚 | PhB 引脚 |
|---|---|---|
| QEI0 | PD0 / PB6 | PD1 / PB7 |
| QEI1 | PC5 / PE4 | PC6 / PE5 |

2 组 QEI 可接 2 个电机，其余 2 个可用 GPIO 中断或 Timer Capture 实现。

**TivaWare API：**
```c
SysCtlPeripheralEnable(SYSCTL_PERIPH_QEI0);
QEIConfigure(QEI0_BASE, QEI_CONFIG_CAPTURE_A | QEI_CONFIG_NO_RESET, 0xFFFFFFFF);
QEIVelocityConfigure(QEI0_BASE, QEI_VELDIV_1, SysCtlClockGet() / 100);
QEIEnable(QEI0_BASE);
uint32_t pos = QEIPositionGet(QEI0_BASE);       // 位置计数
uint32_t vel = QEIVelocityGet(QEI0_BASE);        // 速度
```

### UART — 蓝牙/WiFi/调试

TM4C123GH6PM 有 **8 组 UART 外设** (UART0 ~ UART7)。

| 外设 | TX 引脚 | RX 引脚 | 典型用途 |
|---|---|---|---|
| UART0 | PA1 | PA0 | 蓝牙通信 |
| UART1 | PB0 | PB1 | 调试输出 |
| UART2 | PD6 | PD7 | WiFi / ESP8266 |
| UART3 | PC6 | PC7 | 备用 |

### I2C — IMU、OLED 等

TM4C123GH6PM 有 **4 组 I2C 外设** (I2C0 ~ I2C3)。

| 外设 | SDA 引脚 | SCL 引脚 |
|---|---|---|
| I2C0 | PA6 | PA7 |
| I2C1 | PA4 | PA5 |
| I2C2 | PE4 | PE5 |
| I2C3 | PD0 | PD1 |

**常用 TivaWare API：**
```c
SysCtlPeripheralEnable(SYSCTL_PERIPH_I2C0);
I2CMasterInitExpClk(I2C0_BASE, SysCtlClockGet(), true);
I2CMasterSlaveAddrSet(I2C0_BASE, 0x68, false);  // MPU6050 地址
I2CMasterDataPut(I2C0_BASE, data);
I2CMasterControl(I2C0_BASE, I2C_MASTER_CMD_SINGLE_SEND);
```

### ADC — 电池电压、传感器

TM4C123GH6PM 有 2 组 ADC 模块 (ADC0, ADC1)，12 位精度。

**参考示例：** `examples/peripherals/adc/`

---

## MG513 电机说明

MG513 电机参数：
- **额定电压：** 12V DC
- **空载电流：** ~300mA (单轴)
- **编码器：** 自带霍尔 AB 相正交编码器，常见分辨率 390 / 520 / 980 PPR

每个电机需要 5 根 MCU 连线：

| 信号 | 类型 | 说明 |
|---|---|---|
| IN1 | GPIO 输出 | 方向控制 (H/L) |
| IN2 | GPIO 输出 | 方向控制 (H/L) |
| PWM | Timer 输出 | 速度控制 |
| ENC_A | QEI / GPIO 捕获 | 编码器 A 相 |
| ENC_B | QEI / GPIO 捕获 | 编码器 B 相 |

**推荐驱动芯片：**
- **TB6612FNG** — 双路 H 桥，单路 1.2A，性价比最高
- **L298N** — 双路 H 桥，单路 2A，体积较大
- 4 个电机需 2 块驱动板

---

## 引脚分配方案（参考）

```
TM4C123GH6PM (64-LQFP)
┌─────────────────────────────────────┐
│                                     │
│  M1_PWM   Timer0A  → PB6 (T0CCP0)  │
│  M1_IN1   GPIO     → PA2           │
│  M1_IN2   GPIO     → PA3           │
│  M1_ENCA  QEI0 PhA → PD0           │
│  M1_ENCB  QEI0 PhB → PD1           │
│                                     │
│  M2_PWM   Timer0B  → PB7 (T0CCP1)  │
│  M2_IN1   GPIO     → PA4           │
│  M2_IN2   GPIO     → PA5           │
│  M2_ENCA  QEI1 PhA → PC5           │
│  M2_ENCB  QEI1 PhB → PC6           │
│                                     │
│  M3_PWM   Timer1A  → PF4 (T1CCP0)  │
│  M3_IN1   GPIO     → PC4           │
│  M3_IN2   GPIO     → PC7           │
│  M3_ENCA  GPIO 捕获 → PE0          │
│  M3_ENCB  GPIO 捕获 → PE1          │
│                                     │
│  M4_PWM   Timer1B  → PF5 (T1CCP1)  │
│  M4_IN1   GPIO     → PD2           │
│  M4_IN2   GPIO     → PD3           │
│  M4_ENCA  GPIO 捕获 → PE2          │
│  M4_ENCB  GPIO 捕获 → PE3          │
│                                     │
│  蓝牙 TX  UART1    → PB0           │
│  蓝牙 RX  UART1    → PB1           │
│                                     │
│  MPU6050 I2C0 SDA  → PA6           │
│  MPU6050 I2C0 SCL  → PA7           │
│                                     │
│  超声波 Trig      → PE4           │
│  超声波 Echo      → PE5 (Timer Capture)│
│                                     │
│  循迹1            → PA0 (ADC0)     │
│  循迹2            → PA1 (ADC1)     │
│  循迹3            → PE2 (ADC3)     │
│  循迹4            → PE3 (ADC4)     │
│                                     │
│  OLED SDA  I2C3    → PD0           │
│  OLED SCL  I2C3    → PD1           │
│                                     │
│  蜂鸣器           → PF3 (PWM)      │
│  电池电压 ADC     → PE0 (ADC2)     │
└─────────────────────────────────────┘
```

**注意：** 以上为早期参考方案，实际引脚以 SysConfig 生成的 pinmux 为准。请使用 SysConfig GUI 重新规划。

---

## 参考资料

### 本地 TivaWare 文档

| 文档 | 路径 / 说明 |
|---|---|
| TivaWare DriverLib 用户指南 | `TivaWare_C_Series-2.2.0.295/docs/SW-TM4C-DRL-UG-2.2.0.295.pdf` |
| TM4C123G LaunchPad 用户指南 | `TivaWare_C_Series-2.2.0.295/docs/SW-EK-TM4C123GXL-UG-2.2.0.295.pdf` |
| TivaWare 示例说明 | `TivaWare_C_Series-2.2.0.295/docs/SW-TM4C-EXAMPLES-UG-2.2.0.295.pdf` |

### 在线 TI 文档

| 文档 | 说明 | 链接 |
|---|---|---|
| TM4C123GH6PM 数据手册 | 芯片电气特性与引脚 | https://www.ti.com/lit/ds/symlink/tm4c123gh6pm.pdf |
| TM4C123x 技术参考手册 | 外设寄存器详解 | https://www.ti.com/lit/ug/spmu298/spmu298.pdf |
| TM4C123G LaunchPad 指南 | 开发板使用说明 | 见 LaunchPad 包装盒内文档 |

### TivaWare 驱动头文件

| 模块 | 头文件 | API 函数数 |
|---|---|---|
| GPIO | `driverlib/gpio.h` | ~ 7 个 |
| Timer (PWM) | `driverlib/timer.h` | ~ 19 个 |
| QEI | `driverlib/qei.h` | ~ 16 个 |
| UART | `driverlib/uart.h` | ~ 20 个 |
| I2C | `driverlib/i2c.h` | ~ 12 个 |
| ADC | `driverlib/adc.h` | ~ 4 个 |
| SysCtl | `driverlib/sysctl.h` | 时钟配置 |
| Interrupt | `driverlib/interrupt.h` | ~ 9 个 |

---

## 软件架构建议

```
main.c
├── Car_Init()           ← 初始化入口
│   ├── Clock_Init()     ← SysCtlClockSet(16MHz → PLL → 80MHz)
│   ├── PinoutSet()      ← SysConfig 生成的引脚配置
│   ├── Motor_Init()     ← Timer PWM + GPIO 方向
│   ├── Encoder_Init()   ← QEI + GPIO 捕获
│   ├── Sensor_Init()    ← I2C / ADC / UART
│   └── PID_Init()       ← PID 参数
│
└── while(1)             ← 主循环
    ├── ReadSensors()    ← 循迹 / IMU / 超声波
    ├── EstimatePose()   ← 编码器里程 + IMU 融合
    ├── PlanMotion()     ← 路径规划
    └── SetMotors()      ← PID → PWM 输出
```

---

## 下一步

1. 用 SysConfig GUI 打开并编辑 `.syscfg/tm4c123gh6pm.syscfg` 引脚配置
2. 按外设模块在 `drivers/` 目录下创建对应的 .c/.h 文件
3. 参考 TivaWare DriverLib API 编写驱动
4. 在 `main.c` 中集成调用

建议先实现电机 PWM + 编码器测速，再逐步添加 PID 闭环与传感器融合。
