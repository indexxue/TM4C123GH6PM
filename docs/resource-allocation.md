# TM4C123GH6PM 资源分配方案

> 定时器、DMA、中断优先级与 PCB 设计参考。  
> **引脚以 [syscfg-io-allocation.md](syscfg-io-allocation.md) 为准**（四轮 / 两轮两套配置）。

---

## 一、芯片资源清单

### 1.1 外设模块（64-pin LQFP）

| 外设 | 数量 | 本项目 |
|------|------|--------|
| Timer | 6 组 ×2 通道 | 电机 PWM |
| Wide Timer | 6 组 | 备用 / 捕获 |
| UART | 8 组 | 蓝牙 UART1、调试 UART7 |
| I2C | 6 组 | I2C0（IMU + OLED） |
| ADC | ADC0 + ADC1 | ADC1：电池、按键、巡线 |
| QEI | 2 组 | 编码器（每车 2 路） |
| uDMA | 32 通道 | ADC、UART RX（规划） |
| SSI | 4 组 | SSI0 磁力计 |

### 1.2 GPIO（64-pin）

| 端口 | 可用引脚 | 合计 |
|------|----------|------|
| PA~PD | 各 8 | 32 |
| PE | 0~5 | 6 |
| PF | 0~4 | 5 |
| **合计** | | **43** |

---

## 二、当前工程外设占用（摘要）

### 四轮 `car-4wd`

| 资源 | 分配 |
|------|------|
| PWM0 | M1~M4 @ PB6/PB7/PB4/PB5（Timer0 + Timer1） |
| QEI1 | M3 编码器 @ PC5/PC6 |
| QEI0 | M4 编码器 @ PD6/PD7 |
| UART1 | 蓝牙 @ PB0/PB1 |
| UART7 | 调试 @ PE0/PE1 |
| I2C0 | PB2/PB3 |
| SSI0 | PA2~PA5 |
| ADC1 | 电池 AIN0、按键 AIN1、巡线 AIN4~9 |

### 两轮 `car-2wd`

| 资源 | 分配 |
|------|------|
| PWM0 | M1/M2 @ PB6/PB7 |
| QEI1 | M1 @ PC5/PC6 |
| QEI0 | M2 @ PD6/PD7 |
| 通信/传感器 | 与四轮相同（I2C、SSI、UART） |
| ADC1 | 6 路模拟输入，5 路巡线传感器 |

完整引脚表见 [syscfg-io-allocation.md](syscfg-io-allocation.md)。

---

## 三、中断与 DMA 规划

### 3.1 中断优先级（建议）

| 优先级 | 中断源 | 用途 |
|--------|--------|------|
| 0 | SysTick | RTOS 节拍 |
| 1~2 | QEI / 编码器 | 测速 |
| 3 | UART1 | 蓝牙收发 |
| 4 | GPIO（超声波 Echo） | 测距 |
| 5 | I2C0 | IMU 读取 |
| 6 | ADC1 | 电池 / 巡线采样 |
| 7 | 其他 GPIO | 按键等 |

### 3.2 DMA 通道（规划）

| 通道 | 方向 | 说明 |
|------|------|------|
| CH0 | ADC1 → RAM | 电池电压周期采样 |
| CH1 | UART1 RX → RAM | 蓝牙接收环形缓冲 |

### 3.3 PWM 参数

| 参数 | 值 |
|------|-----|
| 主晶振 | **8 MHz**（car-4wd / car-2wd 统一） |
| 系统时钟 | 80 MHz（PLL） |
| 频率 | 10 kHz |
| 重装载值 | 8000 |
| 驱动 | TB6612FNG |

### 3.4 编码器

| 参数 | 值 |
|------|-----|
| 接口 | QEI 硬件正交解码 |
| MG513 分辨率 | 约 390 PPR（四倍频后约 1560 计数/转） |
| 速度环周期 | 20 ms（50 Hz） |

---

## 四、定时器占用（四轮）

```
Timer0  A/B → M1/M2 PWM (PB6/PB7)
Timer1  A/B → M3/M4 PWM (PB4/PB5)
Timer2~5      空闲（可按需扩展）
QEI1          M3 编码器 (PC5/PC6)
QEI0          M4 编码器 (PD6/PD7)
```

两轮仅占用 Timer0 A/B 与 QEI1/QEI0。

---

## 五、PCB 设计要点

### 电源拓扑

```
11.1V 电池 → LM2596 5V → 传感器
                └── AMS1117 3.3V → MCU / 蓝牙 / IMU / OLED
         └── TB6612 VM → 电机
         └── 分压 → ADC（电池电压）
```

### 布线

| 规则 | 说明 |
|------|------|
| 电机线 | 远离 MCU 与模拟信号 |
| I2C | ≤ 10 cm，4.7 kΩ 上拉 |
| ADC 巡线 | 100 nF 滤波到地 |
| 去耦 | 每 VCC 引脚 100 nF |

---

## 六、检查清单

- [ ] 引脚与 [syscfg-io-allocation.md](syscfg-io-allocation.md) 一致
- [ ] I2C0 (PB2/PB3) 无 PWM 冲突
- [ ] SWD（PC0/PC1）与编码器无冲突（当前四轮 M4 编码器在 PD6/PD7）
- [ ] 逻辑地与电机地单点共地
- [ ] TB6612 STBY 上拉使能
