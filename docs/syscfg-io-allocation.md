# SysConfig IO 分配

> 本文档严格依据 SysConfig 源文件中的 **`$suggestSolution`（引脚）** 与 **`$name` / `$assign`（符号）** 整理。
> 源文件：
> - 四轮：`tm4c123gh6pm.syscfg`（`projects/car-4wd/.syscfg/` 内为同内容副本）
> - 两轮：`tm4c123gh6pm_2.syscfg`（`projects/car-2wd/.syscfg/` 内为同内容副本）
>
> 编译自动生成的引脚表：`projects/<car>/gpio-allocation.md`。

## 读表说明

| 列 | 含义 |
|----|------|
| **SysConfig 符号** | `.syscfg` 内外设实例名（如 `M1_IN1`、`MyQEI1`） |
| **硬件模块** | `$suggestSolution` 解析出的片上外设（如 PWM0、QEI1、UART1） |
| **引脚** | `$suggestSolution` 或 QEI `$assign` 指定的封装引脚 |

固件层可对 ADC 通道赋予语义（如 AIN0=电池、AIN1=按键）；**引脚与通道以本表 syscfg 为准**。

---

## 四电机配置（`tm4c123gh6pm.syscfg`）

### ADC1（`MyADC1` → 硬件 ADC1）

| ADC 通道 | 引脚 | syscfg 启用 |
|----------|------|-------------|
| AIN0 | PE3 | 是 |
| AIN1 | PE2 | 是 |
| AIN4 | PD3 | 是 |
| AIN5 | PD2 | 是 |
| AIN6 | PD1 | 是 |
| AIN7 | PD0 | 是 |
| AIN8 | PE5 | 是 |
| AIN9 | PE4 | 是 |

未启用：AIN2、AIN3、AIN10、AIN11（`$used = false`）。
四轮 syscfg **无** 巡线/电池专用 GPIO 符号，上表通道仅通过 ADC pinmux 占用引脚。

### 电机方向（GPIO 输出）

| SysConfig 符号 | 引脚 | 电机 |
|----------------|------|------|
| M1_IN1 | PF4 | M1 |
| M1_IN2 | PC7 | M1 |
| M2_IN1 | PC4 | M2 |
| M2_IN2 | PA0 | M2 |
| M3_IN1 | PA1 | M3 |
| M3_IN2 | PA6 | M3 |
| M4_IN1 | PA7 | M4 |
| M4_IN2 | PF0 | M4 |

### 电机 PWM（`MyPWM1` → 硬件 PWM0）

| SysConfig 通道 | 引脚 | 电机 |
|----------------|------|------|
| PWM0 | PB6 | M1 |
| PWM1 | PB7 | M2 |
| PWM2 | PB4 | M3 |
| PWM3 | PB5 | M4 |

未使用：PWM4~PWM7、FAULT0。

### 编码器（QEI）

| SysConfig 符号 | 硬件 | PHA | PHB | 关联电机 |
|----------------|------|-----|-----|----------|
| MyQEI1 | QEI1 | PC5 | PC6 | M1 |
| MyQEI2 | QEI0 | PD6 | PD7 | M2 |

另有两组 **GPIO 命名占位**（与 QEI 实际引脚不同，仅 syscfg 保留名）：

| SysConfig 符号 | 引脚 |
|----------------|------|
| M3_ENCA | PF1 |
| M3_ENCB | PF2 |
| M4_ENCA | PF3 |
| M4_ENCB | PD4 |

### 通信与外设

| SysConfig 符号 | 硬件 | 引脚 |
|----------------|------|------|
| MyI2C0 | I2C0 | PB2 SCL, PB3 SDA |
| MySSI1 | SSI0 | PA2 CLK, PA3 FSS, PA5 TX, PA4 RX |
| BLE_UART | UART1 | PB0 RX, PB1 TX |
| DEBG_UART | UART7 | PE0 RX, PE1 TX |

### 其他 GPIO

| SysConfig 符号 | 引脚 | 方向 |
|----------------|------|------|
| BUZZER | PD5 | 输出 |
| RGB_LED | PC3 | 输出 |
| ULTRA_TRIG | PC2 | 输出 |
| ULTRA_ECHO | PC1 | 输入 |

### 四电机引脚总表

| 引脚 | SysConfig 功能 |
|------|----------------|
| PA0 | M2_IN2 |
| PA1 | M3_IN1 |
| PA2 | SSI0 CLK |
| PA3 | SSI0 FSS |
| PA4 | SSI0 RX |
| PA5 | SSI0 TX |
| PA6 | M3_IN2 |
| PA7 | M4_IN1 |
| PB0 | BLE_UART RX |
| PB1 | BLE_UART TX |
| PB2 | I2C0 SCL |
| PB3 | I2C0 SDA |
| PB4 | PWM2 / M3 |
| PB5 | PWM3 / M4 |
| PB6 | PWM0 / M1 |
| PB7 | PWM1 / M2 |
| PC1 | ULTRA_ECHO |
| PC2 | ULTRA_TRIG |
| PC3 | RGB_LED |
| PC4 | M2_IN1 |
| PC5 | QEI1 PHA (M3) |
| PC6 | QEI1 PHB (M3) |
| PC7 | M1_IN2 |
| PD0 | ADC1 AIN7 |
| PD1 | ADC1 AIN6 |
| PD2 | ADC1 AIN5 |
| PD3 | ADC1 AIN4 |
| PD4 | M4_ENCB（GPIO 名） |
| PD5 | BUZZER |
| PD6 | QEI0 PHA (M4) |
| PD7 | QEI0 PHB (M4) |
| PE0 | DEBG_UART RX |
| PE1 | DEBG_UART TX |
| PE2 | ADC1 AIN1 |
| PE3 | ADC1 AIN0 |
| PE4 | ADC1 AIN9 |
| PE5 | ADC1 AIN8 |
| PF0 | M4_IN2 |
| PF1 | M3_ENCA（GPIO 名） |
| PF2 | M3_ENCB（GPIO 名） |
| PF3 | M4_ENCA（GPIO 名） |
| PF4 | M1_IN1 |

---

## 双电机配置（`tm4c123gh6pm_2.syscfg`）

### ADC1（`MyADC1` → 硬件 ADC1）

| ADC 通道 | 引脚 | SysConfig GPIO 名 |
|----------|------|-------------------|
| AIN0 | PE3 | LINE1 |
| AIN1 | PE2 | LINE2 |
| AIN4 | PD3 | LINE4 |
| AIN5 | PD2 | LINE3 |
| AIN6 | PD1 | BUTTON |
| AIN7 | PD0 | LINE5 |

未启用：AIN2、AIN3、AIN8、AIN9、AIN10、AIN11。

### 电机方向（GPIO 输出）

| SysConfig 符号 | 引脚 | 电机 |
|----------------|------|------|
| M1_IN1 | PF4 | M1 |
| M1_IN2 | PC7 | M1 |
| M2_IN1 | PC4 | M2 |
| M2_IN2 | PA0 | M2 |

### 电机 PWM（`MyPWM1` → 硬件 PWM0）

| SysConfig 通道 | 引脚 | 电机 |
|----------------|------|------|
| PWM0 | PB6 | M1 |
| PWM1 | PB7 | M2 |

未使用：PWM2~PWM7、FAULT0。

### 编码器（QEI + GPIO 名）

| SysConfig 符号 | 硬件 | PHA | PHB | 电机 |
|----------------|------|-----|-----|------|
| MyQEI1 / M1_ENCA,B | QEI1 | PC5 | PC6 | M1 |
| MyQEI2 / M2_ENCA,B | QEI0 | PD6 | PD7 | M2 |

QEI `$assign` 与 GPIO `M1_ENCA`/`M1_ENCB`/`M2_ENCA`/`M2_ENCB` 的 `$suggestSolution` 引脚一致。

### 通信与外设

与四电机相同：

| SysConfig 符号 | 硬件 | 引脚 |
|----------------|------|------|
| MyI2C0 | I2C0 | PB2 SCL, PB3 SDA |
| MySSI1 | SSI0 | PA2 CLK, PA3 FSS, PA5 TX, PA4 RX |
| BLE_UART | UART1 | PB0 RX, PB1 TX |
| DEBG_UART | UART7 | PE0 RX, PE1 TX |

### 其他 GPIO

| SysConfig 符号 | 引脚 | 方向 |
|----------------|------|------|
| BUZZER | PC0 | 输出 |
| RGB_LED | PE4 | 输出 |
| ULTRA_TRIG | PE5 | 输出 |
| ULTRA_ECHO | PD5 | 输入 |

### 双电机引脚总表

| 引脚 | SysConfig 功能 |
|------|----------------|
| PA0 | M2_IN2 |
| PA2 | SSI0 CLK |
| PA3 | SSI0 FSS |
| PA4 | SSI0 RX |
| PA5 | SSI0 TX |
| PB0 | BLE_UART RX |
| PB1 | BLE_UART TX |
| PB2 | I2C0 SCL |
| PB3 | I2C0 SDA |
| PB6 | PWM0 / M1 |
| PB7 | PWM1 / M2 |
| PC0 | BUZZER |
| PC4 | M2_IN1 |
| PC5 | M1_ENCA / QEI1 PHA |
| PC6 | M1_ENCB / QEI1 PHB |
| PC7 | M1_IN2 |
| PD0 | LINE5 / ADC1 AIN7 |
| PD1 | BUTTON / ADC1 AIN6 |
| PD2 | LINE3 / ADC1 AIN5 |
| PD3 | LINE4 / ADC1 AIN4 |
| PD5 | ULTRA_ECHO |
| PD6 | M2_ENCA / QEI0 PHA |
| PD7 | M2_ENCB / QEI0 PHB |
| PE0 | DEBG_UART RX |
| PE1 | DEBG_UART TX |
| PE2 | LINE2 / ADC1 AIN1 |
| PE3 | LINE1 / ADC1 AIN0 |
| PE4 | RGB_LED |
| PE5 | ULTRA_TRIG |
| PF4 | M1_IN1 |

---

## 两版差异摘要（syscfg 对比）

| 项目 | 四电机 `tm4c123gh6pm.syscfg` | 双电机 `tm4c123gh6pm_2.syscfg` |
|------|------------------------------|--------------------------------|
| 电机数 | 4 | 2 |
| PWM | PB4~PB7（4 路） | PB6~PB7（2 路） |
| QEI | M3/M4 @ PC5~6、PD6~7 | M1/M2 @ PC5~6、PD6~7 |
| ADC 启用通道 | 8（AIN0/1/4~9） | 6（AIN0/1/4~7） |
| 巡线 GPIO 名 | 无（仅 ADC 引脚） | LINE1~5 + BUTTON |
| 蜂鸣器 | PD5 | PC0 |
| RGB LED | PC3 | PE4 |
| 超声波 | PC2 Trig / PC1 Echo | PE5 Trig / PD5 Echo |

---

## 固件 ADC 语义（可选，非 syscfg 字段）

若应用层采用统一约定 **AIN0=电池、AIN1=按键、其余通道=巡线**，与 syscfg 的对应关系为：

| 约定 | 四电机 syscfg 引脚 | 双电机 syscfg 现状 |
|------|-------------------|-------------------|
| AIN0 电池 | PE3（无 GPIO 名） | PE3（GPIO 名 `LINE1`） |
| AIN1 按键 | PE2（无 GPIO 名） | PE2（GPIO 名 `LINE2`） |
| 巡线 | AIN4~9 → PD3/PD2/PD1/PD0/PE5/PE4 | AIN4~7 + GPIO LINE3~5、`BUTTON` |

双电机若要坚持上述约定，需在固件中按通道读 ADC，或后续改 syscfg GPIO 命名与引脚。

修改引脚请编辑对应 `.syscfg` 后同步更新本文档。
