# TM4C123GH6PM 芯片资源清单与外设分配方案

## 芯片资源概览 (64-pin LQFP 封装)

### 1.1 GPIO 资源

| 端口 | 引脚数 | 可用范围 (PA~PF) |
|---|---|---|
| PA   | 8 | PA0 ~ PA7 |
| PB   | 8 | PB0 ~ PB7 |
| PC   | 8 | PC0 ~ PC7 |
| PD   | 8 | PD0 ~ PD7 |
| PE   | 6 | PE0 ~ PE5 (PE6/PE7 未引出) |
| PF   |  5 | PF0 ~ PF4 (PF5/PF6/PF7 未引出) |
| **合计** | **43** | |

### 1.2 片上外设

| 外设 | 数量 | 本项目用途 |
|---|---|---|
| Timer (通用定时器) | 6 组 (×2 通道 = 12 路 PWM) | 电机 PWM |
| WTimer (宽位 32/64b) | 6 组 | 编码器捕获 |
| UART | 8 组 | 蓝牙、调试 |
| I2C | 6 组 | IMU、OLED |
| SSI (SPI) | 4 组 | 备用扩展 |
| ADC (12-bit) | 2 组 (×8 通道) | 电池、传感器 |
| QEI (正交编码器) | 2 组 (见 hw_memmap.h，TM4C123 有 QEI 模块) | 2 组硬件 |
| PWM (专用 PWM 模块) | 2 组 | 备用 (用 Timer PWM) |
| Comparator | 3 组 | - |
| CAN | 2 组 | 备用 |
| USB | 1 组 (OTG) | - |
| Hibernate | 1 组 | 低功耗待机 |

---

## 外设分配方案（四轴 MG513 小车）

### 2.1 电机驱动 (4 轴 MG513)

每电机 5 根信号线：方向 ×2 + PWM ×1 + 编码器 ×2

#### 方案 A：Timer PWM + GPIO 编码器捕获

| 电机 | PWM 引脚 | 定时器通道 | 方向 IN1 | 方向 IN2 | 编码器 A | 编码器 B |
|---|---|---|---|---|---|---|
| M1 | PB6 | Timer0A (T0CCP0) | PA2 | PA3 | PD0 (GPIO 捕获) | PD1 (GPIO 捕获) |
| M2 | PB7 | Timer0B (T0CCP1) | PA4 | PA5 | PC5 (GPIO 捕获) | PC6 (GPIO 捕获) |
| M3 | PB4 | Timer1A (T1CCP0) | PC4 | PC7 | PE0 (GPIO 捕获) | PE1 (GPIO 捕获) |
| M4 | PB5 | Timer1B (T1CCP1) | PD2 | PD3 | PE2 (GPIO 捕获) | PE3 (GPIO 捕获) |

**资源占用：** 4 PWM + 8 方向 + 8 编码器 = 20 引脚

> ⚠ 关于 QEI：TM4C123GH6PM 在 `hw_memmap.h` 中定义了 `QEI0_BASE`/`QEI1_BASE`，
> QEI 模块存在但 64-pin 封装引脚有限。若使用 QEI，需确认 pinmux 不冲突。
> 硬件 QEI 引脚：QD0=PD0/PD1, QD1=PC5/PC6 等，需与上述方案协调。

### 2.2 通信接口

| 功能 | 外设 | 引脚 | 数量 |
|---|---|---|---|
| 蓝牙/WiFi | UART0 | PA0 (RX), PA1 (TX) | 2 路 |
| 调试输出 | 复用 UART0 (或通过 SWO) | |
| IMU (MPU6050) | I2C0 | PB2 (SCL), PB3 (SDA) | 2 路 |
| OLED 显示屏 | I2C0 | 共用 (MPU6050 共用 I2C 总线) | 0 路 |

### 2.3 传感器

| 传感器 | 连接方式 | 引脚 | 数量 |
|---|---|---|---|
| 超声波 HC-SR04 Trig | GPIO 输出 | PE4 | 1 |
| 超声波 HC-SR04 Echo | GPIO 输入 + Timer Capture | PE5 (WT0CCP1) | 1 |
| 循迹传感器 ×4 | GPIO 输入 或 ADC | PA6, PA7 / PC0, PC1 (备选) | 4 |
| 电池电压 | ADC 输入 | PE0 (AIN0) | 1 |

### 2.4 调试

| 功能 | 引脚 |
|---|---|
| SWD/SWCLK (调试) | PC0 (SWCLK), PC1 (SWDIO) — 与编码器冲突 |
| 复位 | PB7 (NRST) — 与 M2_PWM 冲突 |

---

## 引脚冲突与注意事项

以下引脚存在复用冲突，设计 PCB 时需特别注意：

| 引脚 | 当前分配 | 冲突功能 | 建议 |
|---|---|---|---|
| PB6 | M1_PWM (T0CCP0) | I2C5SCL | 不使用 I2C5 |
| PB7 | M2_PWM (T0CCP1) | I2C5SDA | 避免 |
| PB4 | M3_PWM (T1CCP0) | CAN0RX | 不使用 CAN |
| PB5 | M4_PWM (T1CCP1) | CAN0TX | 避免 |
| PC0 | 编码器输入 | T4CCP0, SWCLK | 调试 SWD 冲突 |
| PC1 | 编码器输入 | T4CCP1, SWDIO | 避免 |
| PE0 | 电池电压 (AIN0) | M3_ENCA, U7RX | 多功能复用 |
| PE5 | 超声波 Echo | WT0CCP1, U5TX | 优先 GPIO |

---

## 使用 SysConfig 配置

### 4.1 启动 GUI

```powershell
D:\Ti\sysconfig_1.27.1\sysconfig_gui.bat
# File → Open → .syscfg/tm4c123gh6pm.syscfg
```

### 4.2 配置步骤

1. 配置时钟源：OSC0/PB6, OSC1/PB7，16 MHz 晶振
2. 添加外设并分配 PWM 引脚（Timer CCP 通道）
3. 配置 UART0（蓝牙）和 I2C0（IMU/OLED）
4. 分配传感器引脚（GPIO 输入/输出）
5. 配置 ADC（电池电压）
6. Ctrl+S 保存

> 保存后运行 `.\scripts\build.ps1` 会自动调用 SysConfig CLI 生成 `pinout.c` 等文件。

---

## 常用 TivaWare API 速查

以下 API 可直接用于外设驱动开发：

```c
// 时钟配置
SysCtlClockSet(SYSCTL_SYSDIV_5 | SYSCTL_USE_PLL | SYSCTL_OSC_MAIN | SYSCTL_XTAL_16MHZ);
SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOF);
SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOF);

// GPIO
GPIOPinTypeGPIOOutput(GPIO_PORTF_BASE, GPIO_PIN_1);
GPIOPinWrite(GPIO_PORTF_BASE, GPIO_PIN_1, GPIO_PIN_1);
GPIOPinRead(GPIO_PORTF_BASE, GPIO_PIN_4);
GPIOPadConfigSet(GPIO_PORTF_BASE, GPIO_PIN_4, GPIO_STRENGTH_2MA, GPIO_PIN_TYPE_STD_WPU);

// Timer PWM (电机驱动)
SysCtlPeripheralEnable(SYSCTL_PERIPH_TIMER0);
TimerConfigure(TIMER0_BASE, TIMER_CFG_SPLIT_PAIR | TIMER_CFG_A_PWM);
TimerLoadSet(TIMER0_BASE, TIMER_A, 8000);     // 80MHz / 8000 = 10kHz PWM
TimerMatchSet(TIMER0_BASE, TIMER_A, 4000);     // 50% duty
TimerEnable(TIMER0_BASE, TIMER_A);

// UART (蓝牙)
SysCtlPeripheralEnable(SYSCTL_PERIPH_UART0);
UARTConfigSetExpClk(UART0_BASE, SysCtlClockGet(), 115200,
    UART_CONFIG_WLEN_8 | UART_CONFIG_STOP_ONE | UART_CONFIG_PAR_NONE);
UARTEnable(UART0_BASE);
UARTCharPut(UART0_BASE, 'A');
int c = UARTCharGet(UART0_BASE);

// ADC (电池电压)
SysCtlPeripheralEnable(SYSCTL_PERIPH_ADC0);
ADCSequenceConfigure(ADC0_BASE, 3, ADC_TRIGGER_PROCESSOR, 0);
ADCSequenceStepConfigure(ADC0_BASE, 3, 0, ADC_CTL_CH0 | ADC_CTL_IE | ADC_CTL_END);
ADCSequenceEnable(ADC0_BASE);
ADCProcessorTrigger(ADC0_BASE, 3);
while (!ADCIntStatus(ADC0_BASE, 3, false)) {}
ADCSequenceDataGet(ADC0_BASE, 3, &adc_value);
```
