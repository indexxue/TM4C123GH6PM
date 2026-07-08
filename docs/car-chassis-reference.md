# TM4C123GH6PM 小车底盘开发参考

> TivaWare DriverLib API 用法与软件架构参考。  
> **引脚分配见 [syscfg-io-allocation.md](syscfg-io-allocation.md)**，定时器/DMA 见 [resource-allocation.md](resource-allocation.md)。

---

## 外设需求概览

| 模块 | 接口 | 数量 |
|------|------|------|
| 电机 | PWM + GPIO 方向 | 2 或 4 路 |
| 编码器 | QEI | 2 路（每车） |
| 蓝牙 | UART | 1 路 |
| IMU / OLED | I2C | 1 路总线 |
| 巡线 | ADC | 5~6 路 |
| 超声波 | GPIO Trig/Echo | 1 路 |
| 电池 / 按键 | ADC | AIN0 / AIN1 |

---

## TivaWare API 速查

### Timer PWM（电机）

```c
SysCtlPeripheralEnable(SYSCTL_PERIPH_TIMER0);
TimerConfigure(TIMER0_BASE, TIMER_CFG_SPLIT_PAIR | TIMER_CFG_A_PWM);
TimerLoadSet(TIMER0_BASE, TIMER_A, SysCtlClockGet() / 10000);  // 10 kHz
TimerMatchSet(TIMER0_BASE, TIMER_A, duty);
TimerEnable(TIMER0_BASE, TIMER_A);
```

### QEI（编码器）

```c
SysCtlPeripheralEnable(SYSCTL_PERIPH_QEI0);
GPIOPinConfigure(GPIO_PD6_PHA0);
GPIOPinConfigure(GPIO_PD7_PHB0);
GPIOPinTypeQEI(GPIO_PORTD_BASE, GPIO_PIN_6 | GPIO_PIN_7);
QEIConfigure(QEI0_BASE,
    QEI_CONFIG_CAPTURE_A_B | QEI_CONFIG_NO_RESET |
    QEI_CONFIG_QUADRATURE | QEI_CONFIG_NO_SWAP, 0);
QEIEnable(QEI0_BASE);
```

### UART

```c
SysCtlPeripheralEnable(SYSCTL_PERIPH_UART1);
UARTConfigSetExpClk(UART1_BASE, SysCtlClockGet(), 115200,
    UART_CONFIG_WLEN_8 | UART_CONFIG_STOP_ONE | UART_CONFIG_PAR_NONE);
UARTEnable(UART1_BASE);
```

### I2C

```c
SysCtlPeripheralEnable(SYSCTL_PERIPH_I2C0);
I2CMasterInitExpClk(I2C0_BASE, SysCtlClockGet(), true);  // 400 kHz
```

### ADC（巡线 / 电池）

```c
GPIOPinTypeADC(GPIO_PORTE_BASE, GPIO_PIN_3);
SysCtlPeripheralEnable(SYSCTL_PERIPH_ADC1);
ADCSequenceConfigure(ADC1_BASE, 3, ADC_TRIGGER_PROCESSOR, 0);
ADCSequenceStepConfigure(ADC1_BASE, 3, 0, ADC_CTL_CH0 | ADC_CTL_IE | ADC_CTL_END);
ADCSequenceEnable(ADC1_BASE, 3);
```

---

## MG513 电机

| 信号 | 说明 |
|------|------|
| IN1 / IN2 | 方向（GPIO） |
| PWM | 速度（Timer / PWM0） |
| ENC_A / ENC_B | 编码器（QEI） |

推荐驱动：**TB6612FNG**（双路 H 桥，2 块驱动 4 电机）。

---

## 软件架构

当前工程初始化（`Common/src/start.c` → `Start_Init()`，`main/app.c` → `App_Start()`）：

```
Start_Init()
├── bsp_clock_init()       8 MHz 晶振 → 80 MHz（device_profile）
├── bsp_systick_init()
├── Motor_Init()           PWM + 方向 GPIO（board_mask）
├── Encoder_Init()         QEI
├── Line_Init()            数字巡线
├── Board_Periph_Init()    UART / I2C / ADC / SSI
└── log_init()

App_Start()
├── xTaskCreate(PrimaryTask)
└── cmd_uart_line_service_start()
```

规划中的控制循环（50 Hz）：

```
control task
├── 读 ADC 巡线 / 编码器 / IMU
├── PID 速度 / 循迹
└── 写电机 PWM
```

---

## 配置与开发流程

1. 用 SysConfig 打开 `projects/car-4wd/.syscfg/tm4c123gh6pm.syscfg`（或 `car-2wd` 对应文件）
2. 修改 `.syscfg/gpio.json` 或 GUI 后运行 `python scripts/gen_config.py --car-project <car>`
3. 在 `projects/<car>/src/app.c` 编写应用逻辑；设备驱动在 `projects/<car>/source/`

---

## 参考资料

| 文档 | 路径 |
|------|------|
| DriverLib 用户指南 | `sdk/TivaWare_C_Series-2.2.0.295/docs/SW-TM4C-DRL-UG-2.2.0.295.pdf` |
| TM4C123 数据手册 | https://www.ti.com/lit/ds/symlink/tm4c123gh6pm.pdf |
| 技术参考手册 | https://www.ti.com/lit/ug/spmu298/spmu298.pdf |

| 头文件 | 模块 |
|--------|------|
| `driverlib/timer.h` | PWM |
| `driverlib/qei.h` | 编码器 |
| `driverlib/uart.h` | 串口 |
| `driverlib/i2c.h` | I2C |
| `driverlib/adc.h` | ADC |
