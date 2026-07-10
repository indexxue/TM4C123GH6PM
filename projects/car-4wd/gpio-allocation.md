# TM4C123GH6PMI GPIO 分配表

> 自动生成，源：`projects/car-4wd/.syscfg/gpio.json`。编译与生成流程见 [build.md](build.md)。

## 引脚映射

| 引脚 | 功能 | 方向 | 备注 |
|------|------|------|------|
| PF4 | M1_IN1 | 输出 | Motor 1 direction |
| PC7 | M1_IN2 | 输出 | Motor 1 direction |
| PC4 | M2_IN1 | 输出 | Motor 2 direction |
| PA6 | M2_IN2 | 输出 | Motor 2 direction |
| PA7 | M3_IN1 | 输出 | Motor 3 direction |
| PF0 | M3_IN2 | 输出 | Motor 3 direction |
| PF1 | M4_IN1 | 输出 | Motor 4 direction |
| PF2 | M4_IN2 | 输出 | Motor 4 direction |
| PB6 | M1_PWM | 输出 | PWM0 CH0 |
| PB7 | M2_PWM | 输出 | PWM0 CH1 |
| PB4 | M3_PWM | 输出 | PWM0 CH2 |
| PB5 | M4_PWM | 输出 | PWM0 CH3 |
| PC5 | M1_ENCA | 输入 | SW encoder A (RC filter) |
| PC6 | M1_ENCB | 输入 | SW encoder B (RC filter) |
| PD6 | M2_ENCA | 输入 | SW encoder A (RC filter) |
| PD7 | M2_ENCB | 输入 | SW encoder B (RC filter) |
| PF3 | M3_ENCA | 输入 | SW encoder A |
| PD4 | M3_ENCB | 输入 | SW encoder B |
| PD5 | M4_ENCA | 输入 | SW encoder A |
| PB0 | M4_ENCB | 输入 | SW encoder B |
| PE3 | BAT_ADC | 输入 | ADC1 AIN0 battery |
| PE2 | BTN_ADC | 输入 | ADC1 AIN1 button |
| PD3 | LINE1 | 输入 | ADC1 AIN4 line 1 |
| PD2 | LINE2 | 输入 | ADC1 AIN5 line 2 |
| PD1 | LINE3 | 输入 | ADC1 AIN6 line 3 |
| PD0 | LINE4 | 输入 | ADC1 AIN7 line 4 |
| PE5 | LINE5 | 输入 | ADC1 AIN8 line 5 |
| PE4 | LINE6 | 输入 | ADC1 AIN9 line 6 |
| PB1 | BUZZER | 输出 | buzzer |
| PC3 | RGB_LED | 输出 | WS2812 |
| PC2 | ULTRA_TRIG | 输出 | HC-SR04 trigger |
| PC1 | ULTRA_ECHO | 输入 | HC-SR04 echo (SWDIO — deferred init, see Board_Ultra_Init) |
| PA0 | BT_RX | 输入 | UART0 RX |
| PA1 | BT_TX | 输出 | UART0 TX |
| PB2 | I2C_SCL | 输入 | I2C0 SCL |
| PB3 | I2C_SDA | 输入 | I2C0 SDA |
| PE0 | DBG_RX | 输入 | UART7 RX |
| PE1 | DBG_TX | 输出 | UART7 TX |
| PA2 | SSI_CLK | 输出 | SSI0 CLK |
| PA3 | SSI_FSS | 输出 | SSI0 FSS |
| PA4 | SSI_RX | 输入 | SSI0 RX |
| PA5 | SSI_TX | 输出 | SSI0 TX |

### 电机 PWM (4 路)

| 引脚 | 功能 | 方向 |
|------|------|------|
| PB6 | M1_PWM | 输出 |
| PB7 | M2_PWM | 输出 |
| PB4 | M3_PWM | 输出 |
| PB5 | M4_PWM | 输出 |

### 方向控制 (8 路)

| 引脚 | 功能 | 方向 |
|------|------|------|
| PF4 | M1_IN1 | 输出 |
| PC7 | M1_IN2 | 输出 |
| PC4 | M2_IN1 | 输出 |
| PA6 | M2_IN2 | 输出 |
| PA7 | M3_IN1 | 输出 |
| PF0 | M3_IN2 | 输出 |
| PF1 | M4_IN1 | 输出 |
| PF2 | M4_IN2 | 输出 |

### 编码器 (GPIO 软件 + RC 滤波)

| 引脚 | 功能 | 方向 |
|------|------|------|
| PC5 | M1_ENCA | 输入 |
| PC6 | M1_ENCB | 输入 |
| PD6 | M2_ENCA | 输入 |
| PD7 | M2_ENCB | 输入 |
| PF3 | M3_ENCA | 输入 |
| PD4 | M3_ENCB | 输入 |
| PD5 | M4_ENCA | 输入 |
| PB0 | M4_ENCB | 输入 |

### 通信接口

| 引脚 | 功能 | 方向 |
|------|------|------|
| PA0 | BT_RX | 输入 |
| PA1 | BT_TX | 输出 |
| PB2 | I2C_SCL | 输入 |
| PB3 | I2C_SDA | 输入 |
| PE0 | DBG_RX | 输入 |
| PE1 | DBG_TX | 输出 |

### 循迹传感器 (ADC)

| 引脚 | 功能 | 方向 |
|------|------|------|
| PD3 | LINE1 | 输入 |
| PD2 | LINE2 | 输入 |
| PD1 | LINE3 | 输入 |
| PD0 | LINE4 | 输入 |
| PE5 | LINE5 | 输入 |
| PE4 | LINE6 | 输入 |

### 其他传感器

| 引脚 | 功能 | 方向 |
|------|------|------|
| PE3 | BAT_ADC | 输入 |
| PC2 | ULTRA_TRIG | 输出 |
| PC1 | ULTRA_ECHO | 输入 |

### 人机交互

| 引脚 | 功能 | 方向 |
|------|------|------|
| PE2 | BTN_ADC | 输入 |
| PB1 | BUZZER | 输出 |
| PC3 | RGB_LED | 输出 |

## 生成代码

引脚与外设模块映射见 [build.md §5](build.md#5-板级配置与代码生成)。
