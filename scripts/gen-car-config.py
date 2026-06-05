import json, pathlib

cfg_path = pathlib.Path(r"D:\Ti\tm4c123-project\.syscfg\project.json")
out_dir = pathlib.Path(r"D:\Ti\tm4c123-project\src\generated")
cfg = json.loads(cfg_path.read_text(encoding="utf-8-sig"))
out_dir.mkdir(parents=True, exist_ok=True)

L = []
L.append("/* Auto-generated car peripheral config */")
L.append('#include <stdint.h>')
L.append('#include <stdbool.h>')
L.append('#include "inc/hw_memmap.h"')
L.append('#include "inc/hw_types.h"')
L.append('#include "driverlib/sysctl.h"')
L.append('#include "driverlib/gpio.h"')
L.append('#include "driverlib/timer.h"')
L.append('#include "driverlib/uart.h"')
L.append('#include "driverlib/i2c.h"')
L.append('#include "driverlib/adc.h"')
L.append('#include "driverlib/ssi.h"')
L.append('#include "driverlib/udma.h"')
L.append('#include "car_config.h"')
L.append("")

# === Motor PWM ===
L.append("void Motor_Init(void) {")
for p in cfg.get("pwm", []):
    L.append("    SysCtlPeripheralEnable(SYSCTL_PERIPH_%s);" % p["timer"])
    L.append("    TimerConfigure(%s_BASE, TIMER_CFG_SPLIT_PAIR|TIMER_CFG_A_PWM|TIMER_CFG_B_PWM);" % p["timer"])
    period = int(80000000 / p["frequency"])
    L.append("    TimerLoadSet(%s_BASE, TIMER_%s, %d);" % (p["timer"], p["channel"], period))
    L.append("    TimerMatchSet(%s_BASE, TIMER_%s, 0);" % (p["timer"], p["channel"]))
    L.append("    TimerEnable(%s_BASE, TIMER_%s);" % (p["timer"], p["channel"]))
L.append("}")
L.append("")

# === Encoder Capture ===
L.append("void Encoder_Init(void) {")
for e in cfg.get("encoder", []):
    L.append("    SysCtlPeripheralEnable(SYSCTL_PERIPH_%s);" % e["timer"])
    mode = "TIMER_CFG_A_CAP_TIME_UP|TIMER_CFG_B_CAP_TIME_UP"
    L.append("    TimerConfigure(%s_BASE, TIMER_CFG_SPLIT_PAIR|%s);" % (e["timer"], mode))
    L.append("    TimerControlEvent(%s_BASE, TIMER_A, TIMER_EVENT_BOTH_EDGES);" % e["timer"])
    L.append("    TimerControlEvent(%s_BASE, TIMER_B, TIMER_EVENT_BOTH_EDGES);" % e["timer"])
    L.append("    TimerEnable(%s_BASE, TIMER_A); TimerEnable(%s_BASE, TIMER_B);" % (e["timer"], e["timer"]))
L.append("}")
L.append("")

# === UART Bluetooth ===
for u in cfg.get("uart", []):
    L.append("void UART_Init(void) {")
    L.append("    SysCtlPeripheralEnable(SYSCTL_PERIPH_%s); SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOA);" % u["module"])
    L.append("    UARTConfigSetExpClk(%s_BASE, SysCtlClockGet(), %d, UART_CONFIG_WLEN_8|UART_CONFIG_STOP_ONE|UART_CONFIG_PAR_NONE);" % (u["module"], u["baud"]))
    L.append("    UARTFIFOEnable(%s_BASE); UARTEnable(%s_BASE);" % (u["module"], u["module"]))
    L.append("}")
    L.append("")

# === UART Debug (TX only) ===
for d in cfg.get("uart_debug", []):
    L.append("void UART_Debug_Init(void) {")
    L.append("    SysCtlPeripheralEnable(SYSCTL_PERIPH_%s);" % d["module"])
    L.append("    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOE);")
    L.append("    UARTConfigSetExpClk(%s_BASE, SysCtlClockGet(), %d, UART_CONFIG_WLEN_8|UART_CONFIG_STOP_ONE|UART_CONFIG_PAR_NONE);" % (d["module"], d["baud"]))
    L.append("    UARTFIFOEnable(%s_BASE); UARTEnable(%s_BASE);" % (d["module"], d["module"]))
    L.append("}")
    L.append("")
    L.append("void UART_Debug_Putc(char c) {")
    L.append("    UARTCharPut(%s_BASE, c);" % d["module"])
    L.append("}")
    L.append("void UART_Debug_Puts(const char* s) {")
    L.append("    while (*s) { UARTCharPut(%s_BASE, *s++); }" % d["module"])
    L.append("}")
    L.append("")

# === I2C ===
for i in cfg.get("i2c", []):
    L.append("void I2C_Init(void) {")
    L.append("    SysCtlPeripheralEnable(SYSCTL_PERIPH_%s); SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOB);" % i["module"])
    L.append("    I2CMasterInitExpClk(%s_BASE, SysCtlClockGet(), true);" % i["module"])
    L.append("}")
    L.append("")

# === ADC ===
for a in cfg.get("adc", []):
    L.append("void ADC_Init(void) {")
    L.append("    SysCtlPeripheralEnable(SYSCTL_PERIPH_%s); SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOE);" % a["module"])
    L.append("    ADCSequenceConfigure(%s_BASE, 3, ADC_TRIGGER_PROCESSOR, 0);" % a["module"])
    L.append("    ADCSequenceStepConfigure(%s_BASE, 3, 0, ADC_CTL_CH%d|ADC_CTL_IE|ADC_CTL_END);" % (a["module"], a["channel"]))
    L.append("    ADCSequenceEnable(%s_BASE, 3);" % a["module"])
    L.append("}")
    L.append("")

# === SSI (SPI) ===
for s in cfg.get("ssi", []):
    L.append("void SSI_Init(void) {")
    L.append("    SysCtlPeripheralEnable(SYSCTL_PERIPH_%s);" % s["module"])
    L.append("    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOF);")
    L.append("    SSIConfigSetExpClk(%s_BASE, SysCtlClockGet(), %d, SSI_FRF_MOTO_MODE_0, SSI_MODE_MASTER, %d, 8);" % (s["module"], s["speed"], s["speed"]/2))
    L.append("    SSIEnable(%s_BASE);" % s["module"])
    L.append("}")
    L.append("")

# === DMA ===
L.append("void DMA_Init(void) {")
L.append("    SysCtlPeripheralEnable(SYSCTL_PERIPH_UDMA);")
L.append("    /* DMA control table must be allocated in SRAM by user */")
L.append("    /* See uDMA documentation for channel configuration */")
L.append("    /* TODO: configure ADC0 + UART0 DMA channels */")
L.append("}")
L.append("")

(out_dir / "car_config.c").write_text("\n".join(L), encoding="utf-8")

# Generate header
H = ['#ifndef CAR_CONFIG_H', '#define CAR_CONFIG_H', '#include <stdint.h>']
H.append('#define SYSCLK_HZ    80000000')
H.append('#define PWM_FREQ_HZ  10000')
H.append('#define PWM_PERIOD   (SYSCLK_HZ / PWM_FREQ_HZ)')
H.append('')
for s in cfg.get("pwm", []):
    H.append('#define %s_TIMER  %s_BASE' % (s["name"], s["timer"]))
    H.append('#define %s_PWM_CH TIMER_%s' % (s["name"], s["channel"]))
H.append('')
H.append('/* Functions */')
H.append('void Motor_Init(void);     void Encoder_Init(void);')
H.append('void UART_Init(void);      void I2C_Init(void);')
H.append('void ADC_Init(void);       void SSI_Init(void);')
H.append('void DMA_Init(void);')
H.append('void UART_Debug_Init(void);')
H.append('void UART_Debug_Putc(char c);')
H.append('void UART_Debug_Puts(const char* s);')
H.append('#endif')

(out_dir / "car_config.h").write_text("\n".join(H), encoding="utf-8")
print("gen-car-config.py updated")
