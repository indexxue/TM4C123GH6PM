import sys, json, pathlib

ROOT = pathlib.Path(sys.argv[0] if not __file__ else __file__).resolve().parent.parent if False else pathlib.Path(r"D:\Ti\tm4c123-project")
ROOT = pathlib.Path(r"D:\Ti\tm4c123-project")

cfg_path = ROOT / ".syscfg" / "project.json"
out_dir = ROOT / "src" / "generated"

cfg = json.loads(cfg_path.read_text(encoding="utf-8-sig"))
out_dir.mkdir(parents=True, exist_ok=True)

L = ['/* Auto-generated car peripheral config */']
L.extend([
    '#include <stdint.h>', '#include <stdbool.h>',
    '#include "inc/hw_memmap.h"', '#include "inc/hw_types.h"',
    '#include "driverlib/sysctl.h"', '#include "driverlib/gpio.h"',
    '#include "driverlib/timer.h"', '#include "driverlib/uart.h"',
    '#include "driverlib/i2c.h"', '#include "driverlib/adc.h"',
    '#include "car_config.h"', ''
])

L.append('void Motor_Init(void) {')
L.append('    SysCtlPeripheralEnable(SYSCTL_PERIPH_TIMER0);')
L.append('    TimerConfigure(TIMER0_BASE, TIMER_CFG_SPLIT_PAIR|TIMER_CFG_A_PWM|TIMER_CFG_B_PWM);')
L.append('    TimerLoadSet(TIMER0_BASE, TIMER_A, 8000); TimerLoadSet(TIMER0_BASE, TIMER_B, 8000);')
L.append('    TimerMatchSet(TIMER0_BASE, TIMER_A, 0); TimerMatchSet(TIMER0_BASE, TIMER_B, 0);')
L.append('    TimerEnable(TIMER0_BASE, TIMER_A); TimerEnable(TIMER0_BASE, TIMER_B);')
L.append('    SysCtlPeripheralEnable(SYSCTL_PERIPH_TIMER2);')
L.append('    TimerConfigure(TIMER2_BASE, TIMER_CFG_SPLIT_PAIR|TIMER_CFG_A_PWM|TIMER_CFG_B_PWM);')
L.append('    TimerLoadSet(TIMER2_BASE, TIMER_A, 8000); TimerLoadSet(TIMER2_BASE, TIMER_B, 8000);')
L.append('    TimerMatchSet(TIMER2_BASE, TIMER_A, 0); TimerMatchSet(TIMER2_BASE, TIMER_B, 0);')
L.append('    TimerEnable(TIMER2_BASE, TIMER_A); TimerEnable(TIMER2_BASE, TIMER_B);')
L.append('}')

L.append('')
L.append('void Encoder_Init(void) {')
for timer in ["WTIMER3", "WTIMER4", "WTIMER5"]:
    L.append('    SysCtlPeripheralEnable(SYSCTL_PERIPH_' + timer + ');')
    L.append('    TimerConfigure(' + timer + '_BASE, TIMER_CFG_SPLIT_PAIR|TIMER_CFG_A_CAP_TIME_UP|TIMER_CFG_B_CAP_TIME_UP);')
    L.append('    TimerControlEvent(' + timer + '_BASE, TIMER_A, TIMER_EVENT_BOTH_EDGES);')
    L.append('    TimerControlEvent(' + timer + '_BASE, TIMER_B, TIMER_EVENT_BOTH_EDGES);')
    L.append('    TimerEnable(' + timer + '_BASE, TIMER_A); TimerEnable(' + timer + '_BASE, TIMER_B);')
L.append('}')

L.append('')
L.append('void UART_Init(void) {')
L.append('    SysCtlPeripheralEnable(SYSCTL_PERIPH_UART0); SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOA);')
L.append('    UARTConfigSetExpClk(UART0_BASE, SysCtlClockGet(), 115200, UART_CONFIG_WLEN_8|UART_CONFIG_STOP_ONE|UART_CONFIG_PAR_NONE);')
L.append('    UARTFIFOEnable(UART0_BASE); UARTEnable(UART0_BASE);')
L.append('}')
L.append('')
L.append('void I2C_Init(void) {')
L.append('    SysCtlPeripheralEnable(SYSCTL_PERIPH_I2C0); SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOB);')
L.append('    I2CMasterInitExpClk(I2C0_BASE, SysCtlClockGet(), true);')
L.append('}')
L.append('')
L.append('void ADC_Init(void) {')
L.append('    SysCtlPeripheralEnable(SYSCTL_PERIPH_ADC0); SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOE);')
L.append('    ADCSequenceConfigure(ADC0_BASE, 3, ADC_TRIGGER_PROCESSOR, 0);')
L.append('    ADCSequenceStepConfigure(ADC0_BASE, 3, 0, ADC_CTL_CH0|ADC_CTL_IE|ADC_CTL_END);')
L.append('    ADCSequenceEnable(ADC0_BASE, 3);')
L.append('}')

(out_dir / "car_config.c").write_text("\n".join(L), encoding="utf-8")

H = ['#ifndef CAR_CONFIG_H', '#define CAR_CONFIG_H', '#include <stdint.h>']
H.append('#define SYSCLK_HZ    80000000')
H.append('#define PWM_FREQ_HZ  10000')
H.append('#define PWM_PERIOD   (SYSCLK_HZ / PWM_FREQ_HZ)')
H.append('')
H.append('/* Motor Pin Map */')
H.append('#define M1_PWM_PORT  GPIO_PORTB_BASE  #define M1_PWM_PIN   GPIO_PIN_6')
H.append('#define M1_IN1_PORT  GPIO_PORTA_BASE  #define M1_IN1_PIN   GPIO_PIN_2')
H.append('#define M1_IN2_PORT  GPIO_PORTA_BASE  #define M1_IN2_PIN   GPIO_PIN_3')
H.append('#define M2_PWM_PORT  GPIO_PORTB_BASE  #define M2_PWM_PIN   GPIO_PIN_7')
H.append('#define M2_IN1_PORT  GPIO_PORTA_BASE  #define M2_IN1_PIN   GPIO_PIN_4')
H.append('#define M2_IN2_PORT  GPIO_PORTA_BASE  #define M2_IN2_PIN   GPIO_PIN_5')
H.append('#define M3_PWM_PORT  GPIO_PORTB_BASE  #define M3_PWM_PIN   GPIO_PIN_0')
H.append('#define M3_IN1_PORT  GPIO_PORTC_BASE  #define M3_IN1_PIN   GPIO_PIN_4')
H.append('#define M3_IN2_PORT  GPIO_PORTC_BASE  #define M3_IN2_PIN   GPIO_PIN_7')
H.append('#define M4_PWM_PORT  GPIO_PORTB_BASE  #define M4_PWM_PIN   GPIO_PIN_1')
H.append('#define M4_IN1_PORT  GPIO_PORTD_BASE  #define M4_IN1_PIN   GPIO_PIN_0')
H.append('#define M4_IN2_PORT  GPIO_PORTD_BASE  #define M4_IN2_PIN   GPIO_PIN_1')
H.append('')
H.append('/* Functions */')
H.append('void Motor_Init(void); void Encoder_Init(void);')
H.append('void UART_Init(void); void I2C_Init(void); void ADC_Init(void);')
H.append('#endif')

(out_dir / "car_config.h").write_text("\n".join(H), encoding="utf-8")
print("car_config.c/h generated")
