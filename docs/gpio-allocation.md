# TM4C123GH6PMI GPIO ?????

## ??????

| ?? | ?? | ?? | ???? |
|------|------|------|----------|
| PA0 | BT_RX | ?? |  |
| PA1 | BT_TX | ?? |  |
| PA2 | M1_IN1 | ?? | Motor 1 direction |
| PA3 | M1_IN2 | ?? | Motor 1 direction |
| PA4 | M2_IN1 | ?? | Motor 2 direction |
| PA5 | M2_IN2 | ?? | Motor 2 direction |
| PA6 | LINE1 | ?? | line sensor 1 |
| PA7 | LINE2 | ?? | line sensor 2 |
| PB0 | M3_PWM | ?? | Timer2A T2CCP0 |
| PB1 | M4_PWM | ?? | Timer2B T2CCP1 |
| PB2 | I2C_SCL | ?? | I2C0 SCL (IMU+OLED) |
| PB3 | I2C_SDA | ?? | I2C0 SDA (IMU+OLED) |
| PB4 | BTN_UP | ?? | ????? |
| PB5 | BTN_OK | ?? | ???? |
| PB6 | M1_PWM | ?? | Timer0A T0CCP0, I2C5SCL |
| PB7 | M2_PWM | ?? | Timer0B T0CCP1, I2C5SDA |
| PC0 | M4_ENCA | ?? | Timer4A capture, SWCLK |
| PC1 | M4_ENCB | ?? | Timer4B capture, SWDIO |
| PC4 | M3_IN1 | ?? | Motor 3 direction |
| PC5 | BTN_DN | ?? | ???? |
| PC6 | LINE3 | ?? | line sensor 3 |
| PC7 | M3_IN2 | ?? | Motor 3 direction |
| PD0 | M4_IN1 | ?? | Motor 4 direction |
| PD1 | M4_IN2 | ?? | Motor 4 direction |
| PD2 | M1_ENCA | ?? | WTimer3A capture |
| PD3 | M1_ENCB | ?? | WTimer3B capture |
| PD4 | M2_ENCA | ?? | WTimer4A capture |
| PD5 | M2_ENCB | ?? | WTimer4B capture |
| PD6 | M3_ENCA | ?? | WTimer5A capture |
| PD7 | M3_ENCB | ?? | WTimer5B capture |
| PE0 | BAT_ADC | ?? | ADC0 AIN0 battery |
| PE1 | DBG_TX | ?? |  |
| PE2 | LINE4 | ?? | line sensor 4 |
| PE3 | BUZZER | ?? | buzzer PWM |
| PE4 | ULTRA_TRIG | ?? | HC-SR04 trigger |
| PE5 | ULTRA_ECHO | ?? | HC-SR04 echo interrupt |
| PF4 | RGB_LED | ?? | WS2812 single-bus RGB LED |

## ?????

### ?? PWM (4?)

| ?? | ?? | ?? |
|------|------|------|
| PB0 | M3_PWM | ?? |
| PB1 | M4_PWM | ?? |
| PB6 | M1_PWM | ?? |
| PB7 | M2_PWM | ?? |

### ???? (8?)

| ?? | ?? | ?? |
|------|------|------|
| PA2 | M1_IN1 | ?? |
| PA3 | M1_IN2 | ?? |
| PA4 | M2_IN1 | ?? |
| PA5 | M2_IN2 | ?? |
| PA6 | LINE1 | ?? |
| PA7 | LINE2 | ?? |
| PC4 | M3_IN1 | ?? |
| PC6 | LINE3 | ?? |
| PC7 | M3_IN2 | ?? |
| PD0 | M4_IN1 | ?? |
| PD1 | M4_IN2 | ?? |
| PE2 | LINE4 | ?? |

### ????? (8?)

| ?? | ?? | ?? |
|------|------|------|
| PC0 | M4_ENCA | ?? |
| PC1 | M4_ENCB | ?? |
| PD2 | M1_ENCA | ?? |
| PD3 | M1_ENCB | ?? |
| PD4 | M2_ENCA | ?? |
| PD5 | M2_ENCB | ?? |
| PD6 | M3_ENCA | ?? |
| PD7 | M3_ENCB | ?? |

### ????

| ?? | ?? | ?? |
|------|------|------|
| PA0 | BT_RX | ?? |
| PA1 | BT_TX | ?? |
| PB2 | I2C_SCL | ?? |
| PB3 | I2C_SDA | ?? |
| PE1 | DBG_TX | ?? |

### ???

| ?? | ?? | ?? |
|------|------|------|
| PA6 | LINE1 | ?? |
| PA7 | LINE2 | ?? |
| PC6 | LINE3 | ?? |
| PE0 | BAT_ADC | ?? |
| PE2 | LINE4 | ?? |
| PE4 | ULTRA_TRIG | ?? |
| PE5 | ULTRA_ECHO | ?? |

### ????

| ?? | ?? | ?? |
|------|------|------|
| PB4 | BTN_UP | ?? |
| PB5 | BTN_OK | ?? |
| PC5 | BTN_DN | ?? |
| PE3 | BUZZER | ?? |
| PF4 | RGB_LED | ?? |

## ???????? GPIO?

| ?? | ?? | ?? |
|------|------|------|
| PF0 | SSI1RX | SPI ??????? |
| PF1 | SSI1TX | SPI ??????? |
| PF2 | SSI1CLK | SPI ????? |
| PF3 | SSI1FSS | SPI ????? |
| PE1 | UART7 TX | ?????? |
| PC2 | Timer5A | M4 ????? A |
| PC3 | Timer5B | M4 ????? B |
| PB2 | I2C0 SCL | IMU + OLED ?? |
| PB3 | I2C0 SDA | IMU + OLED ?? |
| PA0 | UART0 RX | ???? |
| PA1 | UART0 TX | ???? |

## ????

- ?? GPIO: 37 / 43
- ?? GPIO: 6
- ??????: PC0, PC1 (SWD ???), PF0~PF3 (SSI1 ?)
