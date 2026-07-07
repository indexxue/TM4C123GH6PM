# TM4C123GH6PMI GPIO 分配表

> 自动生成，源：`projects/car-2wd/.syscfg/gpio.json`。编译与生成流程见 [build.md](build.md)。

## 引脚映射

| 引脚 | 功能 | 方向 | 备注 |
|------|------|------|------|
| PF4 | M1_IN1 | 输出 | Motor 1 direction |
| PC7 | M1_IN2 | 输出 | Motor 1 direction |
| PC4 | M2_IN1 | 输出 | Motor 2 direction |
| PA0 | M2_IN2 | 输出 | Motor 2 direction |
| PB6 | M1_PWM | 输出 | PWM0 CH0 |
| PB7 | M2_PWM | 输出 | PWM0 CH1 |
| PC5 | M1_ENCA | 输入 | QEI1 PHA |
| PC6 | M1_ENCB | 输入 | QEI1 PHB |
| PD6 | M2_ENCA | 输入 | QEI0 PHA |
| PD7 | M2_ENCB | 输入 | QEI0 PHB |
| PE3 | LINE1 | 输入 | ADC1 AIN0 line 1 / battery |
| PE2 | LINE2 | 输入 | ADC1 AIN1 line 2 / button |
| PD2 | LINE3 | 输入 | ADC1 AIN5 line 3 |
| PD3 | LINE4 | 输入 | ADC1 AIN4 line 4 |
| PD0 | LINE5 | 输入 | ADC1 AIN7 line 5 |
| PD1 | BUTTON | 输入 | ADC1 AIN6 extra button |
| PC0 | BUZZER | 输出 | buzzer |
| PE4 | RGB_LED | 输出 | WS2812 |
| PE5 | ULTRA_TRIG | 输出 | HC-SR04 trigger |
| PD5 | ULTRA_ECHO | 输入 | HC-SR04 echo |
| PB0 | BT_RX | 输入 | UART1 RX |
| PB1 | BT_TX | 输出 | UART1 TX |
| PB2 | I2C_SCL | 输入 | I2C0 SCL |
| PB3 | I2C_SDA | 输入 | I2C0 SDA |
| PE0 | DBG_RX | 输入 | UART7 RX |
| PE1 | DBG_TX | 输出 | UART7 TX |
| PA2 | SSI_CLK | 输出 | SSI0 CLK |
| PA3 | SSI_FSS | 输出 | SSI0 FSS |
| PA4 | SSI_RX | 输入 | SSI0 RX |
| PA5 | SSI_TX | 输出 | SSI0 TX |

### 电机 PWM (2 路)

| 引脚 | 功能 | 方向 |
|------|------|------|
| PB6 | M1_PWM | 输出 |
| PB7 | M2_PWM | 输出 |

### 方向控制 (4 路)

| 引脚 | 功能 | 方向 |
|------|------|------|
| PF4 | M1_IN1 | 输出 |
| PC7 | M1_IN2 | 输出 |
| PC4 | M2_IN1 | 输出 |
| PA0 | M2_IN2 | 输出 |

### 编码器 (QEI)

| 引脚 | 功能 | 方向 |
|------|------|------|
| PC5 | M1_ENCA | 输入 |
| PC6 | M1_ENCB | 输入 |
| PD6 | M2_ENCA | 输入 |
| PD7 | M2_ENCB | 输入 |

### 通信接口

| 引脚 | 功能 | 方向 |
|------|------|------|
| PB0 | BT_RX | 输入 |
| PB1 | BT_TX | 输出 |
| PB2 | I2C_SCL | 输入 |
| PB3 | I2C_SDA | 输入 |
| PE0 | DBG_RX | 输入 |
| PE1 | DBG_TX | 输出 |

### 循迹传感器 (ADC)

| 引脚 | 功能 | 方向 |
|------|------|------|
| PE3 | LINE1 | 输入 |
| PE2 | LINE2 | 输入 |
| PD2 | LINE3 | 输入 |
| PD3 | LINE4 | 输入 |
| PD0 | LINE5 | 输入 |

### 其他传感器

| 引脚 | 功能 | 方向 |
|------|------|------|
| PE5 | ULTRA_TRIG | 输出 |
| PD5 | ULTRA_ECHO | 输入 |

### 人机交互

| 引脚 | 功能 | 方向 |
|------|------|------|
| PD1 | BUTTON | 输入 |
| PC0 | BUZZER | 输出 |
| PE4 | RGB_LED | 输出 |

## 生成代码

引脚与外设模块映射见 [build.md §5](build.md#5-板级配置与代码生成)。
