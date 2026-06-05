# TM4C123GH6PM 高性能小车资源分配方案

目标：4 轴 MG513 电机 + 多传感器小车的完整外设规划。
本文档在基础方案之上，**补充中断优先级、DMA 通道、定时器全面规划**。

---

## 一、芯片资源清单

### 1.1 外设模块（64-pin LQFP）

| 外设 | 基地址 / 模块 | 数量 | 64-pin 可用 |
|---|---|---|---|
| Timer (通用) | TIMER0~5_BASE | 6 组 (×2 通道 = 12 路) | ✓ 充足 |
| Wide Timer (32b) | WTIMER0~5_BASE | 6 组 (×2 通道 = 12 路) | ✓ 充足 |
| UART | UART0~7_BASE | 8 组 | ✓ 用 3-4 组足够 |
| I2C | I2C0~5_BASE | 6 组 | ✓ 用 3 组足够 |
| ADC (12-bit) | ADC0_BASE, ADC1_BASE | 2 组 (多通道) | ✓ |
| QEI (正交编码器) | QEI0_BASE, QEI1_BASE | 2 组 | ✓ 64-pin 可用 |
| uDMA | UDMA_BASE (0x400FF000) | 1 控制器 (32 通道) | ✓ |
| PWM 专用模块 | PWM0_BASE (若可用) | 2 组 | ✓ 备用方案 |

### 1.2 可用 GPIO（64-pin 封装，PA~PF）

| 端口 | 范围 | 可用 GPIO |
|---|---|---|
| PA | 0-7 | 8 |
| PB | 0-7 | 8 |
| PC | 0-7 | 8 |
| PD | 0-7 | 8 |
| PE | 0-5 | 6 |
| PF | 0-4 | 5 |
| **合计** | **43** | |

---

## 二、外设分配方案（最终版）

### 2.1 总览

```
Timer0A/B → M1 PWM → PB6/PB7           ← PWM 输出
Timer1A/B → M2 PWM → PB4/PB5           ← PWM 输出
Timer2A/B → M3 PWM → PB0/PB1
Timer3A/B → M4 PWM → PB2/PB3

                 ↓  8 路 PWM

Wide Timer3A/B → M1 ENC A/B → PD2/PD3  ← 编码器捕获
Wide Timer4A/B → M2 ENC A/B → PD4/PD5  ← 编码器捕获
Wide Timer5A/B → M3 ENC A/B → PD6/PD7  ← 编码器捕获
Timer4A/B 硬件捕获 M4 ENC A/B → PC0/PC1   ← ⚠ 与 SWD 冲突

                 ↓  8 编码器通道

UDMA → ADC0 → PE0 (AIN0, 电池电压)     ← 周期性采样 + DMA
UDMA → UART0 → PA0/PA1 (蓝牙)          ← DMA 收发

I2C0 → PB2/PB3 → MPU6050 + OLED        ← 共用总线

GPIO 输入 → 超声波 Echo                  ← 中断捕获
GPIO 输出 → 超声波 Trig、方向控制

```

### 2.2 详细引脚分配

#### 电机与编码器（8 组 Timer 通道 + 8 GPIO）

| 信号 | TM4C123 引脚 | 外设功能 | 定时器 | 说明 |
|---|---|---|---|---|
| M1_PWM | PB6 | T0CCP0 (Timer PWM) | Timer0A | 10 kHz PWM |
| M1_IN1 | PA2 | GPIO 输出 | — | 方向 H/L |
| M1_IN2 | PA3 | GPIO 输出 | — | 方向 H/L |
| M1_ENCA | PD2 | WT3CCP0 (Capture) | WTimer3A | 编码器 A |
| M1_ENCB | PD3 | WT3CCP1 (Capture) | WTimer3B | 编码器 B |
| **M2_PWM** | PB7 | T0CCP1 (Timer PWM) | Timer0B | 10 kHz PWM |
| M2_IN1 | PA4 | GPIO 输出 | — | 方向 |
| M2_IN2 | PA5 | GPIO 输出 | — | 方向 |
| M2_ENCA | PD4 | WT4CCP0 (Capture) | WTimer4A | 编码器 A |
| M2_ENCB | PD5 | WT4CCP1 (Capture) | WTimer4B | 编码器 B |
| **M3_PWM** | PB0 | T2CCP0 (Timer PWM) | Timer2A | 10 kHz PWM |
| M3_IN1 | PC4 | GPIO 输出 | — | 方向 |
| M3_IN2 | PC7 | GPIO 输出 | — | 方向 |
| M3_ENCA | PD6 | WT5CCP0 (Capture) | WTimer5A | 编码器 A |
| M3_ENCB | PD7 | WT5CCP1 (Capture) | WTimer5B | 编码器 B |
| **M4_PWM** | PB1 | T2CCP1 (Timer PWM) | Timer2B | 10 kHz PWM |
| M4_IN1 | PD0 | GPIO 输出 | — | 方向 |
| M4_IN2 | PD1 | GPIO 输出 | — | 方向 |
| M4_ENCA | PC0 | T4CCP0 (Capture) | Timer4A | ⚠ 与 SWCLK 冲突 |
| M4_ENCB | PC1 | T4CCP1 (Capture) | Timer4B | ⚠ 与 SWDIO 冲突 |

> ⚠ **SWD 调试冲突**：PC0/PC1 同时是 SWCLK/SWDIO 调试引脚。
> 方案 A：开发阶段保留 M4 编码器，量产时改用其他引脚。
> 方案 B：M4 编码器改到 GPIO 中断（PE2/PE3），释放 SWD 引脚。

#### 通信接口

| 功能 | 外设 | 引脚 | 速率 | 说明 |
|---|---|---|---|---|
| 蓝牙 | UART0 | PA0 (RX), PA1 (TX) | 115200 bps | DMA 收发 |
| **IMU** | **I2C0** | **PB2 (SCL), PB3 (SDA)** | **400 kHz** | **MPU6050 @ 0x68** |
| OLED | I2C0 | 与 IMU 共用 | 400 kHz | SSD1306 @ 0x3C |
| 调试输出 | UART1 / SWO | PF0 (U1RTS) / PF1 (U1CTS) | 备用 | 可选 |

> I2C0 (PB2/PB3) 不与 M4_PWM (PB2/T3CCP0) 冲突：
> **已调整** M4 的 PWM 到 Timer2B (PB1)，M3 PWM 到 Timer2A (PB0)，
> 使 PB2/PB3 专用于 I2C0 总线。

#### 传感器

| 传感器 | 类型 | 引脚 | 方式 | 精度 |
|---|---|---|---|---|
| 超声波 Trig | GPIO 输出 | PE4 | 脉冲 | — |
| 超声波 Echo | GPIO 输入 | PE5 | 中断计时 | 用 SysTick 辅助计时 |
| 循迹传感器 ×4 | GPIO 输入 | PA6, PA7, PC6, PE2 | 数字 | 0/1 检测 |
| 电池电压 | ADC0 AIN0 | PE0 | 周期性 + DMA | 10 位精度 |

#### 其他

| 功能 | 引脚 | 说明 |
|---|---|---|
| 蜂鸣器 | PE3 | GPIO 输出 / Timer PWM |
| SWCLK | PC0 | 与 M4_ENCA 冲突 |
| SWDIO | PC1 | 与 M4_ENCB 冲突 |
| 复位 | NRST | 硬件复位 |

---

## 三、中断与 DMA 规划

### 3.1 中断优先级

| 优先级 | 中断源 | 用途 |
|---|---|---|
| 0 (最高) | SysTick | 系统节拍 (1 kHz) |
| 1 | Timer4A/B (M4 编码器) | 编码器边沿捕获 |
| 2 | WTimer3~5 (M1~M3 编码器) | 编码器计数 |
| 3 | UART0 (蓝牙) | 数据收发 |
| 4 | GPIO 中断 (超声波 Echo) | 测距 |
| 5 | I2C0 (IMU) | 数据读取 |
| 6 | ADC0 (电池) | 电压采样 |
| 7 (最低) | GPIO (按键/循迹) | 轮询备用 |

### 3.2 DMA 通道分配

| 通道 | 方向 | 源/目的 | 触发方式 |
|---|---|---|---|
| UDMA CH0 | UART0 RX 搬运 | UART0 RX | 中断 → RAM 环形缓冲 |
| UDMA CH1 | UART0 TX 搬运 | UART0 TX | RAM → 发送 |
| UDMA CH2 | ADC0 采样搬运 | Timer 触发 | ADC → RAM 环形缓冲 |
| UDMA CH3 | ADC0 备用 | 软件触发 | ADC → RAM |

### 3.3 PWM 参数

| 参数 | 值 | 说明 |
|---|---|---|
| PWM 频率 | 10 kHz | 电机驱动最佳 |
| 系统时钟 | 80 MHz | 主频 |
| 重装载值 | 8000 | 80 MHz / 10 kHz |
| 最小占空比步进 | 0.0125% | 1/8000 |
| 驱动芯片 | TB6612FNG | 兼容 10kHz |

### 3.4 编码器参数

| 参数 | 值 | 说明 |
|---|---|---|
| 捕获模式 | 双边沿 (Both Edges) | 正交编码器 ×4 |
| MG513 分辨率 | 390 PPR (常见) | 每转脉冲 |
| 倍频后计数 | 1560 | 390 × 4 (四倍频) |
| 速度采样周期 | 20 ms (50 Hz) | PID 控制周期 |
| 速度分辨率 | ±0.5 RPM | 5 Hz 更新频率 |

---

## 四、定时器资源占用图

```
Timer0  (TIMER0_BASE,  0x40030000)
├── A: T0CCP0 → PB6 → M1_PWM         ← PWM 输出 (10 kHz)
├── B: T0CCP1 → PB7 → M2_PWM         ← PWM 输出 (10 kHz)
└── 状态: 占用

Timer1  (TIMER1_BASE,  0x40031000)
├── A: T1CCP0 → PB4 → M3_PWM         ← PWM 输出 (10 kHz)
├── B: T1CCP1 → PB5 → M4_PWM         ← PWM 输出 (10 kHz)
└── 状态: 占用

Timer2  (TIMER2_BASE,  0x40032000)
├── A: T2CCP0 → PB0 → M3_PWM         ← 占用
├── B: T2CCP1 → PB1 → M4_PWM         ← 占用
└── 状态: 占用

Timer3  (TIMER3_BASE,  0x40033000)
├── A: T3CCP0 → PB2 → 空闲           ← 空闲
├── B: T3CCP1 → PB3 → 空闲           ← 空闲
└── 状态: 空闲

Timer4  (TIMER4_BASE,  0x40034000)
├── A: T4CCP0 → PC0 → M4_ENCA        ← 编码器捕获 (冲突)
├── B: T4CCP1 → PC1 → M4_ENCB        ← 编码器捕获 (冲突)
└── 状态: 中断 1

Timer5  (TIMER5_BASE,  0x40035000)
├── A: T5CCP0 → PC2 → 空闲           ← 空闲
├── B: T5CCP1 → PC3 → 空闲           ← 空闲
└── 状态: 空闲

WTimer3 (WTIMER3_BASE, 0x4004D000)
├── A: WT3CCP0 → PD2 → M1_ENCA       ← 编码器捕获 (占用)
├── B: WT3CCP1 → PD3 → M1_ENCB       ← 编码器捕获 (占用)
└── 状态: 中断 2

WTimer4 (WTIMER4_BASE, 0x4004E000)
├── A: WT4CCP0 → PD4 → M2_ENCA       ← 编码器捕获 (占用)
├── B: WT4CCP1 → PD5 → M2_ENCB       ← 编码器捕获 (占用)
└── 状态: 中断 2

WTimer5 (WTIMER5_BASE, 0x4004F000)
├── A: WT5CCP0 → PD6 → M3_ENCA       ← 编码器捕获 (占用)
├── B: WT5CCP1 → PD7 → M3_ENCB       ← 编码器捕获 (占用)
└── 状态: 中断 2

SysTick
└── 1 kHz 系统节拍                   ← 优先级 0 (最高)
```

---

## 五、引脚冲突汇总表

| 引脚 | 当前分配 | 冲突功能 | 严重程度 | 建议 |
|---|---|---|---|---|
| PB0 | M3_PWM (T2CCP0) | U1RX | 低 | 不用 UART1 |
| PB1 | M4_PWM (T2CCP1) | U1TX | 低 | 不用 UART1 |
| PB2 | I2C0 SCL | T3CCP0 | 低 | ✓ **已解决** |
| PB3 | I2C0 SDA | T3CCP1 | 低 | ✓ **已解决** |
| PB6 | M1_PWM (T0CCP0) | I2C5SCL | 低 | 不使用 I2C5 |
| PB7 | M2_PWM (T0CCP1) | I2C5SDA | 低 | 不使用 I2C5 |
| PC0 | M4_ENCA (T4CCP0) | SWCLK | **高** | 调试/FW 烧录 |
| PC1 | M4_ENCB (T4CCP1) | SWDIO | **高** | 调试/FW 烧录 |
| PD0 | M4_IN1 | I2C3SCL, WT2CCP0 | 低 | 仅 GPIO |
| PD1 | M4_IN2 | I2C3SDA, WT2CCP1 | 低 | 仅 GPIO |
| PE0 | 电池电压 (AIN0) | U7RX, WT4CCP0 | 低 | 优先 ADC |
| PE5 | 超声波 Echo | WT2CCP1, U5TX, I2C2SCL | 低 | 优先 GPIO 中断 |

---

## 六、软件架构示例

### 6.1 主循环

```c
void main(void) {
    Clock_Init();              // 16 MHz → PLL → 80 MHz
    PinoutSet();              // SysConfig 生成
    Car_Init();               // 电机 + 传感器 + 通信

    uint32_t tick_last = 0;

    while (1) {
        uint32_t now = SysTick_Get();

        // 每 1 ms 读 IMU 数据 (I2C DMA)
        if (now - tick_last >= 1) { IMU_Read(); }

        // 每 20 ms 更新 PID 控制
        if (now - tick_last >= 20) {
            Encoder_GetSpeed(&m1_speed, &m2_speed, &m3_speed, &m4_speed);
            PID_Update(&pid_m1, target_speed[0], m1_speed);
            Motor_SetPWM(M1, pid_m1.output);
            // ... M2~M4 同理
        }

        // 每 50 ms 读超声波
        // 每 100 ms 刷新 OLED
        // 每 200 ms 上报蓝牙
        __asm("wfi");          // 等待中断，低功耗
    }
}
```

### 6.2 关键 TivaWare API 示例

```c
// === Timer PWM (电机驱动) ===
SysCtlPeripheralEnable(SYSCTL_PERIPH_TIMER0);
TimerConfigure(TIMER0_BASE, TIMER_CFG_SPLIT_PAIR | TIMER_CFG_A_PWM);
TimerLoadSet(TIMER0_BASE, TIMER_A, 8000);     // 10 kHz
TimerMatchSet(TIMER0_BASE, TIMER_A, duty);    // 0~8000
TimerEnable(TIMER0_BASE, TIMER_A);

// === 编码器捕获 (Wide Timer Input Capture) ===
SysCtlPeripheralEnable(SYSCTL_PERIPH_WTIMER3);
TimerConfigure(WTIMER3_BASE, TIMER_CFG_SPLIT_PAIR | TIMER_CFG_A_CAP_TIME_UP);
TimerControlEvent(WTIMER3_BASE, TIMER_A, TIMER_EVENT_BOTH_EDGES);
TimerIntEnable(WTIMER3_BASE, TIMER_CAPA_EVENT);
TimerIntRegister(WTIMER3_BASE, TIMER_A, M1_Encoder_IRQ);
TimerEnable(WTIMER3_BASE, TIMER_A);

// === ADC + DMA (电池电压) ===
SysCtlPeripheralEnable(SYSCTL_PERIPH_ADC0);
ADCSequenceConfigure(ADC0_BASE, 3, ADC_TRIGGER_TIMER, 0);
ADCSequenceStepConfigure(ADC0_BASE, 3, 0, ADC_CTL_CH0 | ADC_CTL_IE | ADC_CTL_END);
ADCSequenceEnable(ADC0_BASE);
ADCIntRegister(ADC0_BASE, 3, ADC_IRQ);

// === UART + DMA (蓝牙) ===
SysCtlPeripheralEnable(SYSCTL_PERIPH_UART0);
UARTConfigSetExpClk(UART0_BASE, SysCtlClockGet(), 115200,
    UART_CONFIG_WLEN_8 | UART_CONFIG_STOP_ONE | UART_CONFIG_PAR_NONE);
UARTFIFOLevelSet(UART0_BASE, UART_FIFO_TX1_8, UART_FIFO_RX1_8);
UARTIntRegister(UART0_BASE, UART_IRQ);
UARTIntEnable(UART0_BASE, UART_INT_RX | UART_INT_RT);
UARTEnable(UART0_BASE);

// === I2C (IMU + OLED) ===
SysCtlPeripheralEnable(SYSCTL_PERIPH_I2C0);
I2CMasterInitExpClk(I2C0_BASE, SysCtlClockGet(), true);   // 400 kHz
I2CMasterIntRegister(I2C0_BASE, I2C_IRQ);
I2CMasterIntEnable(I2C0_BASE);
```

---

## 七、PCB 设计要点

### 7.1 电源拓扑

```
11.1V 电池输入 → LM2596 降压 5V → 传感器供电 → AMS1117 稳压 3.3V → MCU 及逻辑
          ↓                    ↓
          ↓                    ├── 传感器 VCC
          ↓                    └── TCRT5000 VCC
          ↓
          ├── TB6612 VM 电机电源 → 电机 (4 轴)
          ↓
          └── 分压电路 → 电池电压 → PE0 (ADC 采样)
```

### 7.2 布线建议

| 规则 | 说明 |
|---|---|
| 电源线宽 | 电机 ≥ 1 mm，逻辑/信号 ≥ 0.3 mm |
| GND | 大面积铺铜，数字地与电机地单点连接 |
| 电机走线 | 远离 MCU 和传感器，间距 ≥ 5 mm 以上 |
| I2C 走线 | ≤ 10 cm，上拉 4.7 kΩ |
| ADC 走线 | 加 100 nF 滤波电容到地 |
| 去耦电容 | 每个 VCC/GND 引脚旁 100 nF |

### 7.3 上电时序

```
5V 电源稳定 → 3.3V 稳压稳定 → MCU 复位释放
                ↓
         延时 100 ms → TB6612 STBY 拉高 → 电机使能
```

---

## 八、检查清单

### 设计阶段

- [ ] 确认 43 个 GPIO 分配无遗漏
- [ ] I2C0 (PB2/PB3) 不与 PWM 冲突
- [ ] PC0/PC1 调试冲突已评估（SWD vs M4 编码器）
- [ ] 所有传感器与 MCU 共地
- [ ] 电源拓扑完整

### 硬件清单

1. **电源模块**：11V→5V→3.3V→GND 四级供电
2. **SWD 接口**：5 针 (VDD、SWCLK、SWDIO、GND、RST)
3. **MCU 外围**：晶振 16 MHz + 2×20 pF，复位电路，POR
4. **UART 电平**：TX/RX 交叉连接
5. **电机接口**：每电机 5 针（IN1/IN2/PWM/ENCA/ENCB）
6. **TB6612 的 STBY**：上拉使能，避免上电误转

### 元器件

- [ ] TB6612FNG × 2
- [ ] HC-05 × 1
- [ ] MPU6050 × 1
- [ ] 0.96" OLED (SSD1306) × 1
- [ ] HC-SR04P × 2
- [ ] TCRT5000 模块 × 4
- [ ] 有源蜂鸣器 × 1
- [ ] LM2596 模块 × 1
- [ ] AMS1117-3.3 × 1
- [ ] 3S LiPo 11.1V 2200mAh × 1
- [ ] 16 MHz 晶振 + 2×20 pF 电容
- [ ] 杜邦线、排针
- [ ] WS2812 RGB LED（可选）
