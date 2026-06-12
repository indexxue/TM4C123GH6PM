# TM4C123GH6PMI GPIO 分配表

> 自动生成，源：`.syscfg/gpio.json`。编译与生成流程见 [build.md](build.md)。

## 引脚映射

| 引脚 | 功能 | 方向 | 备注 |
|------|------|------|------|
| PA0 | BT_RX | 输入 |  |
| PA1 | BT_TX | 输出 |  |
| PA2 | M1_IN1 | 输出 | Motor 1 direction |
| PA3 | M1_IN2 | 输出 | Motor 1 direction |
| PA4 | M2_IN1 | 输出 | Motor 2 direction |
| PA5 | M2_IN2 | 输出 | Motor 2 direction |
| PA6 | LINE1 | 输入 | line sensor 1 |
| PA7 | LINE2 | 输入 | line sensor 2 |
| PB0 | M3_PWM | 输出 | Timer2A T2CCP0 |
| PB1 | M4_PWM | 输出 | Timer2B T2CCP1 |
| PB2 | I2C_SCL | 输入 | I2C0 SCL (IMU+OLED) |
| PB3 | I2C_SDA | 输入 | I2C0 SDA (IMU+OLED) |
| PB4 | BTN_UP | 输入 | 上键 |
| PB5 | BTN_OK | 输入 | 确认键 |
| PB6 | M1_PWM | 输出 | Timer0A T0CCP0 |
| PB7 | M2_PWM | 输出 | Timer0B T0CCP1 |
| PC0 | M4_ENCA | 输入 | Timer4A capture, SWCLK |
| PC1 | M4_ENCB | 输入 | Timer4B capture, SWDIO |
| PC2 | LINE5 | 输入 | line sensor 5 |
| PC3 | LINE6 | 输入 | line sensor 6 |
| PC4 | M3_IN1 | 输出 | Motor 3 direction |
| PC5 | BTN_DN | 输入 | 下键 |
| PC6 | LINE3 | 输入 | line sensor 3 |
| PC7 | M3_IN2 | 输出 | Motor 3 direction |
| PD0 | M4_IN1 | 输出 | Motor 4 direction |
| PD1 | M4_IN2 | 输出 | Motor 4 direction |
| PD2 | M1_ENCA | 输入 | WTimer3A capture |
| PD3 | M1_ENCB | 输入 | WTimer3B capture |
| PD4 | M2_ENCA | 输入 | WTimer4A capture |
| PD5 | M2_ENCB | 输入 | WTimer4B capture |
| PD6 | M3_ENCA | 输入 | WTimer5A capture |
| PD7 | M3_ENCB | 输入 | WTimer5B capture |
| PE0 | BAT_ADC | 输入 | ADC0 AIN0 battery |
| PE1 | DBG_TX | 输出 | UART7 TX debug |
| PE2 | LINE4 | 输入 | line sensor 4 |
| PE3 | BUZZER | 输出 | buzzer PWM |
| PE4 | ULTRA_TRIG | 输出 | HC-SR04 trigger |
| PE5 | ULTRA_ECHO | 输入 | HC-SR04 echo interrupt |
| PF4 | RGB_LED | 输出 | WS2812 single-bus RGB LED |

### 电机 PWM (4 路)

| 引脚 | 功能 | 方向 |
|------|------|------|
| PB0 | M3_PWM | 输出 |
| PB1 | M4_PWM | 输出 |
| PB6 | M1_PWM | 输出 |
| PB7 | M2_PWM | 输出 |

### 方向控制 (8 路)

| 引脚 | 功能 | 方向 |
|------|------|------|
| PA2 | M1_IN1 | 输出 |
| PA3 | M1_IN2 | 输出 |
| PA4 | M2_IN1 | 输出 |
| PA5 | M2_IN2 | 输出 |
| PC4 | M3_IN1 | 输出 |
| PC7 | M3_IN2 | 输出 |
| PD0 | M4_IN1 | 输出 |
| PD1 | M4_IN2 | 输出 |

### 编码器输入 (8 路)

| 引脚 | 功能 | 方向 |
|------|------|------|
| PC0 | M4_ENCA | 输入 |
| PC1 | M4_ENCB | 输入 |
| PD2 | M1_ENCA | 输入 |
| PD3 | M1_ENCB | 输入 |
| PD4 | M2_ENCA | 输入 |
| PD5 | M2_ENCB | 输入 |
| PD6 | M3_ENCA | 输入 |
| PD7 | M3_ENCB | 输入 |

### 通信接口

| 引脚 | 功能 | 方向 |
|------|------|------|
| PA0 | BT_RX | 输入 |
| PA1 | BT_TX | 输出 |
| PB2 | I2C_SCL | 输入 |
| PB3 | I2C_SDA | 输入 |
| PE1 | DBG_TX | 输出 |

### 循迹传感器

| 引脚 | 功能 | 方向 |
|------|------|------|
| PA6 | LINE1 | 输入 |
| PA7 | LINE2 | 输入 |
| PC6 | LINE3 | 输入 |
| PE2 | LINE4 | 输入 |
| PC2 | LINE5 | 输入 |
| PC3 | LINE6 | 输入 |

### 其他传感器

| 引脚 | 功能 | 方向 |
|------|------|------|
| PE0 | BAT_ADC | 输入 |
| PE4 | ULTRA_TRIG | 输出 |
| PE5 | ULTRA_ECHO | 输入 |

### 人机交互

| 引脚 | 功能 | 方向 |
|------|------|------|
| PB4 | BTN_UP | 输入 |
| PB5 | BTN_OK | 输入 |
| PC5 | BTN_DN | 输入 |
| PE3 | BUZZER | 输出 |
| PF4 | RGB_LED | 输出 |

## 生成代码

引脚与外设模块映射见 [build.md §5](build.md#5-板级配置与代码生成)。
