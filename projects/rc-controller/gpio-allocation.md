# TM4C123GH6PMI GPIO 分配表

> 自动生成，源：`projects/rc-controller/.syscfg/gpio.json`。编译与生成流程见 [build.md](build.md)。

## 引脚映射

| 引脚 | 功能 | 方向 | 备注 |
|------|------|------|------|
| PA0 | BT_RX | 输入 | UART0 蓝牙 RX |
| PA1 | BT_TX | 输出 | UART0 蓝牙 TX |
| PE0 | DBG_RX | 输入 | UART7 调试 RX |
| PE1 | DBG_TX | 输出 | UART7 调试 TX |
| PB2 | I2C_SCL | 输入 | I2C0 SCL（软件 I2C） |
| PB3 | I2C_SDA | 输入 | I2C0 SDA（软件 I2C） |
| PC3 | RGB_LED | 输出 | WS2812 RGB |
| PA2 | SSI_CLK | 输入 | SSI0 CLK（ST7789/NRF24 共享） |
| PA3 | LCD_CS | 输出 | ST7789 软件片选（原 SSI FSS 脚） |
| PA4 | SSI_RX | 输入 | SSI0 MOSI |
| PA5 | SSI_TX | 输出 | SSI0 MISO |
| PF0 | LCD_RST | 输出 | ST7789 RST |
| PF1 | LCD_DC | 输出 | ST7789 DC |
| PF2 | LCD_BL | 输出 | ST7789 背光 |
| PA6 | NRF_CS | 输出 | NRF24 片选 |
| PB0 | NRF_CE | 输出 | NRF24 CE |
| PB1 | NRF_IRQ | 输入 | NRF24 IRQ |
| PC4 | JS1_BTN | 输入 | 操纵杆1 按键（ADC PD0/PD1） |
| PB6 | JS2_BTN | 输入 | 操纵杆2 按键（ADC PD2/PD3） |
| PE3 | BAT_ADC | 输入 | 电池分压 ADC1 AIN0（100K/100K） |

### 蓝牙 / 调试串口

| 引脚 | 功能 | 方向 |
|------|------|------|
| PA0 | BT_RX | 输入 |
| PA1 | BT_TX | 输出 |
| PE0 | DBG_RX | 输入 |
| PE1 | DBG_TX | 输出 |

### I2C 传感器（MPU6050 + QMC5883P）

| 引脚 | 功能 | 方向 |
|------|------|------|
| PB2 | I2C_SCL | 输入 |
| PB3 | I2C_SDA | 输入 |

### 共享 SPI（ST7789 + NRF24）

| 引脚 | 功能 | 方向 |
|------|------|------|
| PA2 | SSI_CLK | 输入 |
| PA4 | SSI_RX | 输入 |
| PA5 | SSI_TX | 输出 |
| PA3 | LCD_CS | 输出 |
| PF0 | LCD_RST | 输出 |
| PF1 | LCD_DC | 输出 |
| PF2 | LCD_BL | 输出 |
| PA6 | NRF_CS | 输出 |
| PB0 | NRF_CE | 输出 |
| PB1 | NRF_IRQ | 输入 |

### 操纵杆

| 引脚 | 功能 | 方向 |
|------|------|------|
| PC4 | JS1_BTN | 输入 |
| PB6 | JS2_BTN | 输入 |

### 电池

| 引脚 | 功能 | 方向 |
|------|------|------|
| PE3 | BAT_ADC | 输入 |

### 人机交互

| 引脚 | 功能 | 方向 |
|------|------|------|
| PC3 | RGB_LED | 输出 |

## 生成代码

引脚与外设模块映射见 [build.md §5](build.md#5-板级配置与代码生成)。
