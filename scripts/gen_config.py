#!/usr/bin/env python3
# Board codegen from .syscfg — see docs/build.md

from __future__ import annotations

import argparse
import json
from collections import defaultdict
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parent.parent
CAR_PROJECTS = {
    "car-4wd": ROOT / "projects" / "car-4wd",
    "car-2wd": ROOT / "projects" / "car-2wd",
}
DEFAULT_CAR_PROJECT = "car-4wd"
DEFAULT_DOCS = ROOT / "docs"


def resolve_car_project(car_project: str) -> dict[str, Path]:
    if car_project not in CAR_PROJECTS:
        raise SystemExit(f"[ERROR] unknown car project: {car_project}")
    base = CAR_PROJECTS[car_project]
    return {
        "root": base,
        "manifest": base / ".syscfg" / "project.json",
        "device_src": base / "board" / "src",
        "device_inc": base / "board" / "inc",
        "app_src": base / "main",
        "build": base / "build",
        "gpio_doc": base / "gpio-allocation.md",
        "ide_compile_db": base / "build" / "ide-compile-db.json",
    }

BOARD_GPIO_GROUPS = ("button", "comm", "sensor", "hmi")
BOARD_MUX_LABELS = frozenset(
    {"BT_RX", "BT_TX", "I2C_SCL", "I2C_SDA", "DBG_RX", "DBG_TX", "BAT_ADC", "BTN_ADC"}
)

INCLUDES_GPIO = [
    "#include <stdbool.h>",
    "#include <stdint.h>",
    '#include "driverlib/gpio.h"',
    '#include "driverlib/pin_map.h"',
    '#include "driverlib/sysctl.h"',
    '#include "inc/hw_memmap.h"',
]

INCLUDES_TIMER = INCLUDES_GPIO + ['#include "driverlib/timer.h"']
INCLUDES_ENCODER = INCLUDES_TIMER + ['#include "driverlib/qei.h"']
INCLUDES_BOARD = INCLUDES_GPIO + [
    '#include "driverlib/uart.h"',
    '#include "driverlib/i2c.h"',
    '#include "driverlib/adc.h"',
    '#include "driverlib/ssi.h"',
    '#include "driverlib/udma.h"',
]

def gpio_helpers(plan: PinPlanner) -> str:
    parts = [
        "static void gpio_enable_ports(uint32_t ports)",
        "{",
        "    if (ports & (1u << 0)) { SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOA); }",
        "    if (ports & (1u << 1)) { SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOB); }",
        "    if (ports & (1u << 2)) { SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOC); }",
        "    if (ports & (1u << 3)) { SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOD); }",
        "    if (ports & (1u << 4)) { SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOE); }",
        "    if (ports & (1u << 5)) { SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOF); }",
        "    while (!SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOA)) {}",
        "}",
    ]
    if any(plan.outputs.values()):
        parts += [
            "",
            "static void gpio_outputs(uint32_t port, uint8_t pins)",
            "{",
            "    GPIOPinTypeGPIOOutput(port, pins);",
            "}",
        ]
    if any(plan.inputs.values()) or any(plan.inputs_pu.values()):
        parts += [
            "",
            "static void gpio_inputs(uint32_t port, uint8_t pins, bool pullup)",
            "{",
            "    GPIOPinTypeGPIOInput(port, pins);",
            "    if (pullup) {",
            "        GPIOPadConfigSet(port, pins, GPIO_STRENGTH_2MA, GPIO_PIN_TYPE_STD_WPU);",
            "    }",
            "}",
        ]
    return "\n".join(parts)

LEGACY_STEMS = [
    "pinout", "pinout_motor", "pinout_encoder", "pinout_line",
    "pinout_button", "pinout_comm", "pinout_sensor", "pinout_hmi",
    "periph_motor", "periph_encoder", "periph_uart_bt", "periph_uart_debug",
    "periph_i2c", "periph_adc", "periph_ssi", "periph_dma", "peripheral",
]
LEGACY_SRC_EXTRA = ("pinout.h", "pinout.c", "summary.csv")


class PinPlanner:
    """Batch GPIO setup by port to minimize register operations in generated C."""

    PORT_BIT = {"A": 0, "B": 1, "C": 2, "D": 3, "E": 4, "F": 5}

    def __init__(self) -> None:
        self.outputs: dict[str, int] = defaultdict(int)
        self.inputs: dict[str, int] = defaultdict(int)
        self.inputs_pu: dict[str, int] = defaultdict(int)
        self.mux_lines: list[str] = []
        self.timer_pins: dict[str, int] = defaultdict(int)
        self._ports: set[str] = set()

    def add_pin(self, port: str, pin: int, direction: str, pull: str | None = None) -> None:
        self._ports.add(port)
        mask = 1 << pin
        if direction == "output":
            self.outputs[port] |= mask
        elif pull == "up":
            self.inputs_pu[port] |= mask
        else:
            self.inputs[port] |= mask

    def add_mux(self, mux: str, port: str, pin: int, kind: str) -> None:
        self._ports.add(port)
        mask = 1 << pin
        self.mux_lines.append(f"    GPIOPinConfigure({mux});")
        if kind == "timer":
            self.timer_pins[port] |= mask
        elif kind == "uart":
            self.mux_lines.append(
                f"    GPIOPinTypeUART(GPIO_PORT{port}_BASE, GPIO_PIN_{pin});"
            )
        elif kind == "i2c":
            self.mux_lines.append(
                f"    GPIOPinTypeI2C(GPIO_PORT{port}_BASE, GPIO_PIN_{pin});"
            )
        elif kind == "adc":
            self.mux_lines.append(
                f"    GPIOPinTypeADC(GPIO_PORT{port}_BASE, GPIO_PIN_{pin});"
            )
        elif kind == "ssi":
            self.mux_lines.append(
                f"    GPIOPinTypeSSI(GPIO_PORT{port}_BASE, GPIO_PIN_{pin});"
            )

    def port_mask(self) -> int:
        return sum(1 << self.PORT_BIT[p] for p in self._ports)

    def emit_gpio_setup(self) -> list[str]:
        lines: list[str] = []
        if not self._ports:
            return lines
        lines.append(f"    gpio_enable_ports(0x{self.port_mask():02X}u);")
        for port in sorted(self._ports):
            base = f"GPIO_PORT{port}_BASE"
            out_m = self.outputs[port]
            in_m = self.inputs[port]
            pu_m = self.inputs_pu[port]
            if out_m:
                lines.append(f"    gpio_outputs({base}, {self._mask(out_m)});")
            if in_m:
                lines.append(f"    gpio_inputs({base}, {self._mask(in_m)}, false);")
            if pu_m:
                lines.append(f"    gpio_inputs({base}, {self._mask(pu_m)}, true);")
        lines.extend(self.mux_lines)
        for port in sorted(self.timer_pins):
            mask = self.timer_pins[port]
            if mask:
                lines.append(
                    f"    GPIOPinTypeTimer(GPIO_PORT{port}_BASE, {self._mask(mask)});"
                )
        return lines

    @staticmethod
    def _mask(bits: int) -> str:
        if bits == 0:
            return "0"
        parts = [f"GPIO_PIN_{i}" for i in range(8) if bits & (1 << i)]
        return " | ".join(parts) if len(parts) > 1 else parts[0]


def load_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8-sig"))


def load_manifest(
    manifest_path: Path, variant: str | None = None
) -> tuple[dict, dict, dict[str, dict], str]:
    cfg_dir = manifest_path.parent
    manifest = load_json(manifest_path)
    if manifest.get("_format") != "tm4c123-sysconfig-v2":
        raise SystemExit("[ERROR] expected tm4c123-sysconfig-v2 manifest")

    if "variants" in manifest:
        variant = variant or manifest.get("default_variant", "4wd")
        if variant not in manifest["variants"]:
            raise SystemExit(f"[ERROR] unknown variant: {variant}")
        vcfg = manifest["variants"][variant]
        board = load_json(cfg_dir / manifest["board"])
        gpio = load_json(cfg_dir / vcfg["gpio"])
        modules = {
            load_json(cfg_dir / rel)["module"]: load_json(cfg_dir / rel)
            for rel in vcfg["modules"]
        }
        return board, gpio, modules, variant

    board = load_json(cfg_dir / manifest["board"])
    gpio = load_json(cfg_dir / manifest["gpio"])
    modules = {
        load_json(cfg_dir / rel)["module"]: load_json(cfg_dir / rel)
        for rel in manifest["modules"]
    }
    return board, gpio, modules, "legacy"


def pin_label_to_str(pin: dict) -> str:
    return f"P{pin['port']}{pin['pin']}"


def motor_dir_labels(gpio: dict) -> list[str]:
    labels = pin_label_map(gpio)
    dirs = [
        lbl for lbl in labels
        if lbl.startswith("M") and len(lbl) >= 6 and lbl[1].isdigit() and "_IN" in lbl
    ]
    return sorted(dirs, key=lambda s: (int(s[1]), s.endswith("_IN2")))


def adc_pin_set(modules: dict[str, dict]) -> set[str]:
    adc = modules.get("adc", {})
    return {item["pin"] for item in adc.get("adc", [])}


def pin_label_map(gpio: dict) -> dict[str, dict]:
    return {p["label"]: p for p in gpio.get("pins", [])}


def parse_pin(pin: str) -> tuple[str, int]:
    return pin[1], int(pin[2:])


def timer_ccp_mux(pin: str, timer: str, channel: str) -> str:
    port, num = parse_pin(pin)
    suffix = "0" if channel == "A" else "1"
    if timer.startswith("WTIMER"):
        tnum = timer.replace("WTIMER", "")
        return f"GPIO_P{port}{num}_WT{tnum}CCP{suffix}"
    tnum = timer.replace("TIMER", "")
    return f"GPIO_P{port}{num}_T{tnum}CCP{suffix}"


def write_module(
    name: str,
    includes: list[str],
    body: list[str],
    protos: list[str],
    src_dir: Path,
    inc_dir: Path,
    *,
    plan: PinPlanner | None = None,
) -> None:
    guard = f"MODULE_{name.upper()}_H"
    src = [
        f"/* Auto-generated {name} module — edit .syscfg, not this file */",
        *includes,
        f'#include "{name}.h"',
        "",
    ]
    if plan is not None and plan._ports:
        src.append(gpio_helpers(plan))
        src.append("")
    src.extend(body)

    (src_dir / f"{name}.c").write_text("\n".join(src), encoding="utf-8")
    (inc_dir / f"{name}.h").write_text(
        "\n".join(
            [
                f"/* Auto-generated by gen_config.py — do not edit */",
                f"#ifndef {guard}",
                f"#define {guard}",
                *protos,
                "#endif",
            ]
        ),
        encoding="utf-8",
    )
    rel = src_dir.relative_to(ROOT) if src_dir.is_relative_to(ROOT) else src_dir
    print(f"  -> {rel}/{name}.c")


def gen_gpio_pins_h(gpio: dict, inc_dir: Path) -> None:
    lines = [
        "/* Auto-generated by gen_config.py — do not edit */",
        "#ifndef GPIO_PINS_H",
        "#define GPIO_PINS_H",
        '#include "driverlib/gpio.h"',
        '#include "inc/hw_memmap.h"',
        "",
    ]
    for p in gpio.get("pins", []):
        label = p["label"]
        lines += [
            f"#define GPIO_{label}_PORT   GPIO_PORT{p['port']}_BASE",
            f"#define GPIO_{label}_PIN    GPIO_PIN_{p['pin']}",
            f"#define GPIO_{label}_MASK   GPIO_PIN_{p['pin']}",
            "",
        ]
    lines.append("#endif")
    (inc_dir / "gpio_pins.h").write_text("\n".join(lines), encoding="utf-8")


def emit_timer_pwm_init(pwm_channels: list[dict], clock: int) -> list[str]:
    """One TimerConfigure per timer; enable each PWM channel once."""
    timers: dict[str, list[dict]] = defaultdict(list)
    for ch in pwm_channels:
        timers[ch["timer"]].append(ch)

    lines: list[str] = []
    for timer, channels in sorted(timers.items()):
        lines.append(f"    SysCtlPeripheralEnable(SYSCTL_PERIPH_{timer});")
        lines.append(
            f"    TimerConfigure({timer}_BASE, "
            "TIMER_CFG_SPLIT_PAIR|TIMER_CFG_A_PWM|TIMER_CFG_B_PWM);"
        )
        for ch in channels:
            period = int(clock / ch["frequency"])
            lines += [
                f"    TimerLoadSet({timer}_BASE, TIMER_{ch['channel']}, {period});",
                f"    TimerMatchSet({timer}_BASE, TIMER_{ch['channel']}, 0);",
                f"    TimerEnable({timer}_BASE, TIMER_{ch['channel']});",
            ]
    return lines


def emit_qei_init(encoders: list[dict]) -> list[str]:
    lines: list[str] = []
    for enc in encoders:
        qei = enc["qei"]
        base = f"{qei}_BASE"
        lines += [
            f"    SysCtlPeripheralEnable(SYSCTL_PERIPH_{qei});",
            f"    QEIConfigure({base}, "
            "QEI_CONFIG_CAPTURE_A_B|QEI_CONFIG_NO_RESET|QEI_CONFIG_QUADRATURE|QEI_CONFIG_NO_SWAP, 0);",
            f"    QEIEnable({base});",
        ]
    return lines


def qei_gpio_mux(pin: str, qei: str, role: str) -> str:
    port, num = parse_pin(pin)
    idx = "1" if qei == "QEI1" else "0"
    suffix = f"PHA{idx}" if role == "a" else f"PHB{idx}"
    return f"GPIO_P{port}{num}_{suffix}"


def add_qei_mux(plan: PinPlanner, enc: dict) -> None:
    pins_by_port: dict[str, int] = defaultdict(int)
    for role, key in (("a", "pin_a"), ("b", "pin_b")):
        pin = enc[key]
        port, pin_num = parse_pin(pin)
        plan._ports.add(port)
        plan.mux_lines.append(f"    GPIOPinConfigure({qei_gpio_mux(pin, enc['qei'], role)});")
        pins_by_port[port] |= 1 << pin_num
    for port, mask in sorted(pins_by_port.items()):
        plan.mux_lines.append(
            f"    GPIOPinTypeQEI(GPIO_PORT{port}_BASE, {PinPlanner._mask(mask)});"
        )


def emit_timer_capture_init(encoders: list[dict]) -> list[str]:
    lines: list[str] = []
    for enc in encoders:
        timer = enc["timer"]
        lines += [
            f"    SysCtlPeripheralEnable(SYSCTL_PERIPH_{timer});",
            f"    TimerConfigure({timer}_BASE, "
            "TIMER_CFG_SPLIT_PAIR|TIMER_CFG_A_CAP_TIME_UP|TIMER_CFG_B_CAP_TIME_UP);",
            f"    TimerControlEvent({timer}_BASE, TIMER_A, TIMER_EVENT_BOTH_EDGES);",
            f"    TimerControlEvent({timer}_BASE, TIMER_B, TIMER_EVENT_BOTH_EDGES);",
            f"    TimerEnable({timer}_BASE, TIMER_A);",
            f"    TimerEnable({timer}_BASE, TIMER_B);",
        ]
    return lines


def gen_motor(gpio: dict, mod: dict, board: dict, src_dir: Path, inc_dir: Path) -> None:
    labels = pin_label_map(gpio)
    clock = board.get("system", {}).get("clock_hz", 80000000)
    freq = mod["pwm"][0]["frequency"] if mod.get("pwm") else 10000
    plan = PinPlanner()

    for label in motor_dir_labels(gpio):
        p = labels[label]
        plan.add_pin(p["port"], p["pin"], p["direction"])

    for ch in mod.get("pwm", []):
        port, pin = parse_pin(ch["pin"])
        plan.add_mux(timer_ccp_mux(ch["pin"], ch["timer"], ch["channel"]), port, pin, "timer")

    body = ["void Motor_Init(void) {", *plan.emit_gpio_setup(), *emit_timer_pwm_init(mod["pwm"], clock), "}"]
    protos = [
        "#include <stdint.h>",
        f"#define SYSCLK_HZ    {clock}",
        f"#define PWM_FREQ_HZ  {freq}",
        "#define PWM_PERIOD   (SYSCLK_HZ / PWM_FREQ_HZ)",
        "",
    ]
    for ch in mod["pwm"]:
        protos += [
            f"#define {ch['name']}_TIMER  {ch['timer']}_BASE",
            f"#define {ch['name']}_PWM_CH TIMER_{ch['channel']}",
        ]
    protos += ["", "void Motor_Init(void);"]
    write_module("motor", INCLUDES_TIMER, body, protos, src_dir, inc_dir, plan=plan)


def gen_encoder(gpio: dict, mod: dict, src_dir: Path, inc_dir: Path) -> None:
    plan = PinPlanner()
    timer_encoders: list[dict] = []
    qei_encoders: list[dict] = []
    for enc in mod.get("encoder", []):
        if enc.get("interface") == "qei":
            qei_encoders.append(enc)
            add_qei_mux(plan, enc)
        else:
            timer_encoders.append(enc)
            for pin_key, channel in (("pin_a", "A"), ("pin_b", "B")):
                port, pin = parse_pin(enc[pin_key])
                mux = timer_ccp_mux(enc[pin_key], enc["timer"], channel)
                plan.add_mux(mux, port, pin, "timer")

    body = ["void Encoder_Init(void) {"]
    body += plan.emit_gpio_setup()
    if timer_encoders:
        body += emit_timer_capture_init(timer_encoders)
    if qei_encoders:
        body += emit_qei_init(qei_encoders)
    body.append("}")
    write_module("encoder", INCLUDES_ENCODER, body, ["void Encoder_Init(void);"], src_dir, inc_dir, plan=plan)


def gen_line(gpio: dict, modules: dict[str, dict], src_dir: Path, inc_dir: Path) -> None:
    labels = pin_label_map(gpio)
    adc_pins = adc_pin_set(modules)
    plan = PinPlanner()
    for label in gpio["pinout_groups"]["line"]:
        p = labels[label]
        if pin_label_to_str(p) in adc_pins:
            continue
        plan.add_pin(p["port"], p["pin"], p["direction"], p.get("pull"))

    body = ["void Line_Init(void) {", *plan.emit_gpio_setup(), "}"]
    write_module("line", INCLUDES_GPIO, body, ["void Line_Init(void);"], src_dir, inc_dir, plan=plan)


def uart_pin_mux(pin: str, module: str, role: str) -> str:
    port, num = parse_pin(pin)
    idx = module.replace("UART", "")
    role_suffix = "RX" if role == "rx" else "TX"
    return f"GPIO_P{port}{num}_U{idx}{role_suffix}"


def i2c_pin_mux(pin: str, module: str, role: str) -> str:
    port, num = parse_pin(pin)
    idx = module.replace("I2C", "")
    suffix = "SCL" if role == "scl" else "SDA"
    return f"GPIO_P{port}{num}_I2C{idx}{suffix}"


def ssi_pin_mux(pin: str, module: str, role: str) -> str:
    port, num = parse_pin(pin)
    idx = module.replace("SSI", "")
    role_map = {"rx": "RX", "tx": "TX", "clk": "CLK", "fss": "FSS"}
    return f"GPIO_P{port}{num}_SSI{idx}{role_map[role]}"


def add_uart_mux(plan: PinPlanner, pin: str, module: str, role: str) -> None:
    port, pin_num = parse_pin(pin)
    mux = uart_pin_mux(pin, module, role)
    plan._ports.add(port)
    plan.mux_lines.append(f"    GPIOPinConfigure({mux});")
    plan.mux_lines.append(f"    GPIOPinTypeUART(GPIO_PORT{port}_BASE, GPIO_PIN_{pin_num});")


def add_i2c_mux(plan: PinPlanner, pin: str, module: str, role: str) -> None:
    port, pin_num = parse_pin(pin)
    mux = i2c_pin_mux(pin, module, role)
    plan._ports.add(port)
    plan.mux_lines.append(f"    GPIOPinConfigure({mux});")
    plan.mux_lines.append(f"    GPIOPinTypeI2C(GPIO_PORT{port}_BASE, GPIO_PIN_{pin_num});")


def gen_board(gpio: dict, modules: dict[str, dict], src_dir: Path, inc_dir: Path) -> None:
    labels = pin_label_map(gpio)
    adc_pins = adc_pin_set(modules)
    plan = PinPlanner()

    for group in BOARD_GPIO_GROUPS:
        for label in gpio["pinout_groups"].get(group, []):
            if label in BOARD_MUX_LABELS:
                continue
            p = labels[label]
            if pin_label_to_str(p) in adc_pins:
                continue
            plan.add_pin(p["port"], p["pin"], p["direction"], p.get("pull"))

    uart = modules.get("uart_bt", {})
    for u in uart.get("uart", []):
        add_uart_mux(plan, u["rx"], u["module"], "rx")
        add_uart_mux(plan, u["tx"], u["module"], "tx")

    dbg = modules.get("uart_debug", {})
    for d in dbg.get("uart_debug", []):
        if d.get("rx"):
            add_uart_mux(plan, d["rx"], d["module"], "rx")
        add_uart_mux(plan, d["tx"], d["module"], "tx")

    i2c = modules.get("i2c", {})
    for item in i2c.get("i2c", []):
        add_i2c_mux(plan, item["scl"], item["module"], "scl")
        add_i2c_mux(plan, item["sda"], item["module"], "sda")

    adc = modules.get("adc", {})
    for item in adc.get("adc", []):
        port, pin = parse_pin(item["pin"])
        plan._ports.add(port)
        plan.mux_lines.append(f"    GPIOPinTypeADC(GPIO_PORT{port}_BASE, GPIO_PIN_{pin});")

    ssi = modules.get("ssi", {})
    for item in ssi.get("ssi", []):
        by_port: dict[str, int] = defaultdict(int)
        for role in ("rx", "tx", "clk", "fss"):
            pin_str = item[role]
            port, pin = parse_pin(pin_str)
            plan._ports.add(port)
            plan.mux_lines.append(f"    GPIOPinConfigure({ssi_pin_mux(pin_str, item['module'], role)});")
            by_port[port] |= 1 << pin
        for port, mask in sorted(by_port.items()):
            plan.mux_lines.append(
                f"    GPIOPinTypeSSI(GPIO_PORT{port}_BASE, {PinPlanner._mask(mask)});"
            )

    body = ["void Board_Periph_Init(void) {", *plan.emit_gpio_setup()]

    for u in uart.get("uart", []):
        base = u["module"]
        body += [
            f"    SysCtlPeripheralEnable(SYSCTL_PERIPH_{base});",
            f"    UARTConfigSetExpClk({base}_BASE, SysCtlClockGet(), {u['baud']}, "
            "UART_CONFIG_WLEN_8|UART_CONFIG_STOP_ONE|UART_CONFIG_PAR_NONE);",
            f"    UARTFIFOEnable({base}_BASE);",
            f"    UARTEnable({base}_BASE);",
        ]

    for d in dbg.get("uart_debug", []):
        base = d["module"]
        body += [
            f"    SysCtlPeripheralEnable(SYSCTL_PERIPH_{base});",
            f"    UARTConfigSetExpClk({base}_BASE, SysCtlClockGet(), {d['baud']}, "
            "UART_CONFIG_WLEN_8|UART_CONFIG_STOP_ONE|UART_CONFIG_PAR_NONE);",
            f"    UARTFIFOEnable({base}_BASE);",
            f"    UARTEnable({base}_BASE);",
        ]

    for item in i2c.get("i2c", []):
        body += [
            f"    SysCtlPeripheralEnable(SYSCTL_PERIPH_{item['module']});",
            f"    I2CMasterInitExpClk({item['module']}_BASE, SysCtlClockGet(), true);",
        ]

    adc_items = adc.get("adc", [])
    if adc_items:
        adc_module = adc_items[0]["module"]
        body += [
            f"    SysCtlPeripheralEnable(SYSCTL_PERIPH_{adc_module});",
            f"    ADCSequenceConfigure({adc_module}_BASE, 3, ADC_TRIGGER_PROCESSOR, 0);",
        ]
        for step, item in enumerate(adc_items):
            end_flag = "|ADC_CTL_END" if step == len(adc_items) - 1 else ""
            body.append(
                f"    ADCSequenceStepConfigure({adc_module}_BASE, 3, {step}, "
                f"ADC_CTL_CH{item['channel']}{end_flag});"
            )
        body += [
            f"    ADCSequenceEnable({adc_module}_BASE, 3);",
            f"    ADCProcessorTrigger({adc_module}_BASE, 3);",
        ]

    for item in ssi.get("ssi", []):
        body += [
            f"    SysCtlPeripheralEnable(SYSCTL_PERIPH_{item['module']});",
            f"    SSIConfigSetExpClk({item['module']}_BASE, SysCtlClockGet(), "
            f"SSI_FRF_MOTO_MODE_0, SSI_MODE_MASTER, {item['speed']}, 8);",
            f"    SSIEnable({item['module']}_BASE);",
        ]

    if modules.get("dma"):
        body.append("    SysCtlPeripheralEnable(SYSCTL_PERIPH_UDMA);")

    body.append("}")

    for u in uart.get("uart", []):
        base = u["module"]
        body += [
            "",
            "void UART_Putc(char c) {",
            f"    UARTCharPut({base}_BASE, c);",
            "}",
            "void UART_Puts(const char* s) {",
            f"    while (*s) {{ UARTCharPut({base}_BASE, *s++); }}",
            "}",
            "int UART_Getc(char *c) {",
            f"    if (UARTCharsAvail({base}_BASE)) {{",
            f"        *c = (char)UARTCharGetNonBlocking({base}_BASE);",
            "        return 1;",
            "    }",
            "    return 0;",
            "}",
        ]

    for d in dbg.get("uart_debug", []):
        base = d["module"]
        body += [
            "",
            "void UART_Debug_Putc(char c) {",
            f"    UARTCharPut({base}_BASE, c);",
            "}",
            "void UART_Debug_Puts(const char* s) {",
            f"    while (*s) {{ UARTCharPut({base}_BASE, *s++); }}",
            "}",
            "int UART_Debug_Getc(char *c) {",
            f"    if (UARTCharsAvail({base}_BASE)) {{",
            f"        *c = (char)UARTCharGetNonBlocking({base}_BASE);",
            "        return 1;",
            "    }",
            "    return 0;",
            "}",
        ]

    protos = ["void Board_Periph_Init(void);"]
    if uart.get("uart"):
        protos += [
            "void UART_Putc(char c);",
            "void UART_Puts(const char* s);",
            "int UART_Getc(char *c);",
        ]
    if dbg.get("uart_debug"):
        protos += [
            "void UART_Debug_Putc(char c);",
            "void UART_Debug_Puts(const char* s);",
            "int UART_Debug_Getc(char *c);",
        ]

    write_module("board", INCLUDES_BOARD, body, protos, src_dir, inc_dir, plan=plan)


def gen_gpio_allocation_md(gpio: dict, out_path: Path, car_project: str) -> None:
    labels = pin_label_map(gpio)
    pins = gpio.get("pins", [])
    src = f"`projects/{car_project}/.syscfg/gpio.json`"
    lines = [
        "# TM4C123GH6PMI GPIO 分配表",
        "",
        f"> 自动生成，源：{src}。编译与生成流程见 [build.md](build.md)。",
        "",
        "## 引脚映射",
        "",
        "| 引脚 | 功能 | 方向 | 备注 |",
        "|------|------|------|------|",
    ]
    for p in pins:
        note = p.get("_usage", "")
        d = "输出" if p["direction"] == "output" else "输入"
        lines.append(f"| P{p['port']}{p['pin']} | {p['label']} | {d} | {note} |")

    for title, group in gpio.get("doc_groups", {}).items():
        lines += ["", f"### {title}", "", "| 引脚 | 功能 | 方向 |", "|------|------|------|"]
        for lbl in group:
            p = labels.get(lbl)
            if p:
                d = "输出" if p["direction"] == "output" else "输入"
                lines.append(f"| P{p['port']}{p['pin']} | {lbl} | {d} |")

    lines += [
        "",
        "## 生成代码",
        "",
        "引脚与外设模块映射见 [build.md §5](build.md#5-板级配置与代码生成)。",
        "",
    ]
    (out_path).write_text("\n".join(lines), encoding="utf-8")


def remove_legacy(src_dir: Path, inc_dir: Path) -> None:
    for stem in LEGACY_STEMS:
        for folder, ext in ((src_dir, ".c"), (inc_dir, ".h")):
            path = folder / f"{stem}{ext}"
            if path.exists():
                path.unlink()
    for name in LEGACY_SRC_EXTRA:
        path = src_dir / name
        if path.exists():
            path.unlink()


def gen_ide_compile_db(paths: dict[str, Path], car_project: str) -> None:
    """Emit per-project build/ide-compile-db.json for IDE navigation."""
    tivaware = Path(r"D:/Ti/TivaWare_C_Series-2.2.0.295")
    freertos = tivaware / "third_party/FreeRTOS/Source"
    freertos_port = freertos / "portable/GCC/ARM_CM4F"
    gcc = ROOT / "tools/bin/arm-none-eabi-gcc.exe"
    gcc_cmd = gcc.as_posix() if gcc.exists() else "arm-none-eabi-gcc"
    device_src = paths["device_src"]
    device_inc = paths["device_inc"]
    app_src = paths["app_src"]
    ide_db = paths["ide_compile_db"]
    bsp_inc = ROOT / "bsp_driver" / "inc"
    bsp_src = ROOT / "bsp_driver" / "src"

    includes = [
        ROOT / "include",
        device_inc,
        bsp_inc,
        ROOT / "Common" / "inc",
        ROOT / "cbb/ws2812b",
        freertos / "include",
        freertos_port,
        tivaware,
        tivaware / "inc",
    ]
    product_id = 2 if car_project == "car-2wd" else 1
    profile_defines = [f"-DDEVICE_PRODUCT_ID={product_id}"]
    flags = [
        "-mcpu=cortex-m4", "-mthumb", "-mfloat-abi=hard", "-mfpu=fpv4-sp-d16",
        "-DTM4C123GH6PM", "-DPART_TM4C123GH6PM",
        "-std=c11", "-Wall", "-Wextra", "-Wpedantic",
        "-ffunction-sections", "-fdata-sections", "-Os", "-g3",
    ] + profile_defines + [f"-I{inc.as_posix()}" for inc in includes]

    sources = [
        app_src / "startup_tm4c123gh6pm.c",
        app_src / "main.c",
        app_src / "app.c",
        app_src / "freertos_hooks.c",
        app_src / "syscalls.c",
        bsp_src / "clock.c",
        bsp_src / "uart.c",
        ROOT / "Common/src/type.c",
        ROOT / "Common/src/device_profile.c",
        ROOT / "Common/src/start.c",
        ROOT / "Common/src/log.c",
        freertos / "tasks.c",
        freertos / "queue.c",
        freertos / "list.c",
        freertos_port / "port.c",
        freertos / "portable/MemMang/heap_4.c",
        device_src / "motor.c",
        device_src / "encoder.c",
        device_src / "line.c",
        device_src / "board.c",
        ROOT / "Common/src/cmd.c",
        ROOT / "Common/src/battery.c",
        ROOT / "Common/src/button.c",
        ROOT / "Common/src/flexible_button.c",
        ROOT / "Common/src/led_scene.c",
        ROOT / "Common/src/crc32.c",
        ROOT / "Common/src/nvs_flash_ops.c",
        ROOT / "Common/src/nvs.c",
        ROOT / "Common/src/ota_meta.c",
        ROOT / "cbb/ws2812b/ws2812b.c",
    ]

    entries = []
    for src in sources:
        if not src.exists():
            continue
        cmd = " ".join([gcc_cmd, *flags, "-c", f'"{src.as_posix()}"'])
        entries.append({"directory": ROOT.as_posix(), "file": src.as_posix(), "command": cmd})

    ide_db.parent.mkdir(parents=True, exist_ok=True)
    ide_db.write_text(json.dumps(entries, indent=4) + "\n", encoding="utf-8")
    print(f"  -> {ide_db.relative_to(ROOT)} ({len(entries)} entries)")


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument(
        "--car-project",
        choices=sorted(CAR_PROJECTS),
        default=DEFAULT_CAR_PROJECT,
        help="car project under projects/ (default: car-4wd)",
    )
    ap.add_argument("--manifest", default=None, help="override .syscfg/project.json path")
    ap.add_argument("--src", default=None, help="override device source output directory")
    ap.add_argument("--inc", default=None, help="override device include output directory")
    ap.add_argument(
        "--ide-db",
        action="store_true",
        help="also generate projects/<car>/build/ide-compile-db.json",
    )
    args = ap.parse_args()

    paths = resolve_car_project(args.car_project)
    manifest = Path(args.manifest) if args.manifest else paths["manifest"]
    if not manifest.exists():
        print(f"[SKIP] no manifest: {manifest}")
        return

    src_dir = Path(args.src) if args.src else paths["device_src"]
    inc_dir = Path(args.inc) if args.inc else paths["device_inc"]
    for d in (src_dir, inc_dir, paths["build"]):
        d.mkdir(parents=True, exist_ok=True)

    print("Generating optimized board modules...")
    board, gpio, modules, variant = load_manifest(manifest)
    print(f"  car-project: {args.car_project}")
    remove_legacy(src_dir, inc_dir)
    gen_gpio_pins_h(gpio, inc_dir)
    gen_motor(gpio, modules["motor"], board, src_dir, inc_dir)
    gen_encoder(gpio, modules["encoder"], src_dir, inc_dir)
    gen_line(gpio, modules, src_dir, inc_dir)
    gen_board(gpio, modules, src_dir, inc_dir)
    gen_gpio_allocation_md(gpio, paths["gpio_doc"], args.car_project)
    if args.ide_db:
        gen_ide_compile_db(paths, args.car_project)
    print("Done.")


if __name__ == "__main__":
    main()
