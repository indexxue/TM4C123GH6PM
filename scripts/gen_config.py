#!/usr/bin/env python3
# Board codegen from .syscfg — see docs/build.md

from __future__ import annotations

import argparse
import json
from collections import defaultdict
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parent.parent
TIVAWARE_VERSION = "2.2.0.295"


def tivaware_root() -> Path:
    return ROOT / "sdk" / f"TivaWare_C_Series-{TIVAWARE_VERSION}"


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

# TM4C123: PC0=SWCLK, PC1=SWDIO — 启动时配成 GPIO 会导致 J-Link 只能烧录一次
TM4C123_SWD_GPIO = frozenset({("C", 0), ("C", 1)})
DEFER_BOOT_GPIO_LABELS = frozenset({"ULTRA_ECHO", "ULTRA_TRIG"})


def defer_gpio_at_boot(label: str, port: str, pin: int) -> bool:
    """Return True if pin must not be touched in Board_Periph_Init (SWD reconnect)."""
    if (port, pin) in TM4C123_SWD_GPIO:
        return True
    if label in DEFER_BOOT_GPIO_LABELS and port == "C" and pin in (1, 2):
        return True
    return False

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
INCLUDES_BOARD_BSP = [
    "#include <stdbool.h>",
    "#include <stdint.h>",
    '#include "bsp_gpio.h"',
    '#include "bsp_uart.h"',
    '#include "bsp_i2c.h"',
    '#include "bsp_adc.h"',
    '#include "bsp_dma.h"',
    '#include "bsp_spi.h"',
    "#include \"driverlib/gpio.h\"",
    '#include "driverlib/pin_map.h"',
    '#include "driverlib/sysctl.h"',
    '#include "driverlib/timer.h"',
    '#include "inc/hw_memmap.h"',
]
INCLUDES_MOTOR_BSP = [
    "#include <stdbool.h>",
    "#include <stdint.h>",
    '#include "bsp_gpio.h"',
    '#include "bsp_timer.h"',
    '#include "driverlib/gpio.h"',
    '#include "driverlib/pin_map.h"',
    '#include "driverlib/sysctl.h"',
    '#include "inc/hw_memmap.h"',
    '#include "driverlib/timer.h"',
]
INCLUDES_ENCODER_BSP = [
    "#include <stdbool.h>",
    "#include <stdint.h>",
    '#include "bsp_gpio.h"',
    '#include "bsp_qei.h"',
    '#include "driverlib/gpio.h"',
    '#include "driverlib/pin_map.h"',
    '#include "driverlib/sysctl.h"',
    '#include "inc/hw_memmap.h"',
]
INCLUDES_BOARD = INCLUDES_GPIO + [
    '#include "driverlib/uart.h"',
    '#include "driverlib/i2c.h"',
    '#include "driverlib/adc.h"',
    '#include "driverlib/udma.h"',
]

def emit_init_guard(call: str) -> list[str]:
    return [
        f"    if (!{call}) {{",
        "        return false;",
        "    }",
    ]


def gpio_helpers(plan: PinPlanner) -> str:
    parts: list[str] = []
    if any(plan.outputs.values()):
        parts += [
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
    "motor", "encoder", "line", "gpio_pins", "periph_bind",
]
LEGACY_SRC_EXTRA = ("pinout.h", "pinout.c", "summary.csv")
LEGACY_INC_EXTRA = ("motor.h", "encoder.h", "line.h", "gpio_pins.h", "periph_bind.h")


class BoardHeader:
    """Accumulate board/inc/board.h sections from codegen."""

    def __init__(self) -> None:
        self._sections: list[tuple[str, list[str]]] = []

    def add_section(self, title: str, lines: list[str]) -> None:
        cleaned = [line for line in lines if line.strip()]
        if cleaned:
            self._sections.append((title, cleaned))

    def write(self, inc_dir: Path) -> None:
        lines = [
            "/* Auto-generated by gen_config.py — do not edit */",
            "#ifndef BOARD_H",
            "#define BOARD_H",
            "",
            "#include <stdbool.h>",
            "#include <stdint.h>",
            "",
        ]
        for title, section in self._sections:
            lines.append(f"/* --- {title} --- */")
            lines.extend(section)
            lines.append("")
        lines.append("#endif")
        (inc_dir / "board.h").write_text("\n".join(lines), encoding="utf-8")


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

    def merge_gpio(self, other: PinPlanner) -> None:
        """Merge GPIO direction maps (for gpio_helpers when init is split)."""
        self._ports |= other._ports
        for port, bits in other.outputs.items():
            self.outputs[port] |= bits
        for port, bits in other.inputs.items():
            self.inputs[port] |= bits
        for port, bits in other.inputs_pu.items():
            self.inputs_pu[port] |= bits

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

    def emit_gpio_setup(self, guarded: bool = False) -> list[str]:
        lines: list[str] = []
        if not self._ports:
            return lines
        enable = f"bsp_gpio_port_enable(0x{self.port_mask():02X}u)"
        if guarded:
            lines.extend(emit_init_guard(enable))
        else:
            lines.append(f"    (void){enable};")
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
    header: BoardHeader,
    *,
    section_title: str,
    plan: PinPlanner | None = None,
) -> None:
    src = [
        f"/* Auto-generated {name} module — edit .syscfg, not this file */",
        *includes,
        '#include "board.h"',
        "",
    ]
    if plan is not None and plan._ports:
        src.append(gpio_helpers(plan))
        src.append("")
    src.extend(body)

    (src_dir / f"{name}.c").write_text("\n".join(src), encoding="utf-8")
    header.add_section(
        section_title,
        [line for line in protos if not line.startswith("#include ")],
    )
    rel = src_dir.relative_to(ROOT) if src_dir.is_relative_to(ROOT) else src_dir
    print(f"  -> {rel}/{name}.c")


def append_gpio_pins_section(header: BoardHeader, gpio: dict) -> None:
    lines = [
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
    header.add_section("GPIO pins", lines)


def append_periph_bind_section(
    header: BoardHeader, modules: dict[str, dict], board: dict
) -> None:
    lines = [
        '#include "bsp_uart.h"',
        '#include "bsp_i2c.h"',
        '#include "bsp_adc.h"',
        '#include "bsp_timer.h"',
        '#include "bsp_qei.h"',
        '#include "bsp_spi.h"',
        "",
        f"#define BOARD_SYSCLK_HZ  {board.get('system', {}).get('clock_hz', 80000000)}",
        "",
    ]

    if modules.get("motor", {}).get("pwm"):
        lines.append("extern const bsp_pwm_config_t BOARD_PWM_CFG;")
    qei_encoders = [
        enc for enc in modules.get("encoder", {}).get("encoder", []) if enc.get("interface") == "qei"
    ]
    if qei_encoders:
        lines.append("extern const bsp_qei_config_t BOARD_QEI_CFG;")
    if modules.get("uart_bt", {}).get("uart"):
        lines.append("extern const bsp_uart_config_t BOARD_UART_BT_CFG;")
    if modules.get("uart_debug", {}).get("uart_debug"):
        lines.append("extern const bsp_uart_config_t BOARD_UART_DEBUG_CFG;")
    if modules.get("i2c", {}).get("i2c"):
        lines.append("extern const bsp_i2c_config_t BOARD_I2C_CFG;")
    if modules.get("adc", {}).get("adc"):
        lines.append("extern const bsp_adc_config_t BOARD_ADC_CFG;")
        battery_items = [
            x for x in modules.get("adc", {}).get("adc", []) if x.get("role") == "battery"
        ]
        if battery_items:
            lines.append("extern const bsp_adc_config_t BOARD_BATTERY_ADC_CFG;")
    if modules.get("ssi", {}).get("ssi"):
        lines.append("extern const bsp_spi_config_t BOARD_SPI_CFG;")

    header.add_section("Peripheral bindings", lines)


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


def gen_motor(gpio: dict, mod: dict, board: dict, src_dir: Path, header: BoardHeader) -> None:
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

    body = [
        "static void motor_apply_dir(const bsp_gpio_pin_t *in1, const bsp_gpio_pin_t *in2, int32_t rpm)",
        "{",
        "    if (rpm > 0) {",
        "        bsp_gpio_write(in1, true);",
        "        bsp_gpio_write(in2, false);",
        "    } else if (rpm < 0) {",
        "        bsp_gpio_write(in1, false);",
        "        bsp_gpio_write(in2, true);",
        "    } else {",
        "        bsp_gpio_write(in1, false);",
        "        bsp_gpio_write(in2, false);",
        "    }",
        "}",
        "",
        "void Motor_Init(void) {",
        *plan.emit_gpio_setup(),
        "    bsp_pwm_init(&BOARD_PWM_CFG);",
        "}",
        "",
        "void Motor_SetSpeed(uint8_t motor_id, int32_t rpm)",
        "{",
        "    uint16_t duty = (rpm == 0) ? 0U : 500U;",
        "    switch (motor_id) {",
    ]
    for i, ch in enumerate(mod.get("pwm", []), start=1):
        name = ch["name"]
        body += [
            f"    case {i}:",
            "        motor_apply_dir(",
            f"            &(const bsp_gpio_pin_t){{GPIO_{name}_IN1_PORT, GPIO_{name}_IN1_MASK}},",
            f"            &(const bsp_gpio_pin_t){{GPIO_{name}_IN2_PORT, GPIO_{name}_IN2_MASK}}, rpm);",
            f"        bsp_pwm_set_duty({name}_TIMER, {name}_PWM_CH, duty);",
            "        break;",
        ]
    body += ["    default:", "        break;", "    }", "}"]

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
    protos += ["", "void Motor_Init(void);", "void Motor_SetSpeed(uint8_t motor_id, int32_t rpm);"]
    write_module("motor", INCLUDES_MOTOR_BSP, body, protos, src_dir, header, section_title="Motor", plan=plan)


def gen_encoder(gpio: dict, mod: dict, src_dir: Path, header: BoardHeader) -> None:
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
        body += ["    bsp_qei_init(&BOARD_QEI_CFG);"]
    body.append("}")

    protos = ["#include <stdint.h>", "void Encoder_Init(void);"]
    if qei_encoders:
        body += [
            "",
            "int32_t Encoder_GetCount(uint8_t index)",
            "{",
            "    switch (index) {",
        ]
        for i, enc in enumerate(qei_encoders):
            body.append(f"    case {i}: return bsp_qei_get_position({enc['qei']}_BASE);")
        body += ["    default: return 0;", "    }", "}"]
        protos.append("int32_t Encoder_GetCount(uint8_t index);")

    write_module("encoder", INCLUDES_ENCODER_BSP, body, protos, src_dir, header, section_title="Encoder", plan=plan)


def gen_line(gpio: dict, modules: dict[str, dict], src_dir: Path, header: BoardHeader) -> None:
    labels = pin_label_map(gpio)
    adc_pins = adc_pin_set(modules)
    plan = PinPlanner()
    for label in gpio["pinout_groups"]["line"]:
        p = labels[label]
        if pin_label_to_str(p) in adc_pins:
            continue
        plan.add_pin(p["port"], p["pin"], p["direction"], p.get("pull"))

    body = ["void Line_Init(void) {", *plan.emit_gpio_setup(), "}"]
    write_module("line", INCLUDES_GPIO, body, ["void Line_Init(void);"], src_dir, header, section_title="Line", plan=plan)


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
    plan._ports.add(port)
    if module == "I2C0":
        # I2C0 走 PB2/PB3 软件 I2C（GPIO 开漏），引脚在 bsp_i2c_init() 配置
        return
    mux = i2c_pin_mux(pin, module, role)
    plan.mux_lines.append(f"    GPIOPinConfigure({mux});")
    plan.mux_lines.append(f"    GPIOPinTypeI2C(GPIO_PORT{port}_BASE, GPIO_PIN_{pin_num});")


def emit_board_config_defs(modules: dict[str, dict], board: dict) -> list[str]:
    lines: list[str] = []
    clock = board.get("system", {}).get("clock_hz", 80000000)

    pwm_channels = modules.get("motor", {}).get("pwm", [])
    if pwm_channels:
        lines.append("static const bsp_pwm_channel_t board_pwm_channels[] = {")
        for ch in pwm_channels:
            lines.append(
                f"    {{ {ch['timer']}_BASE, SYSCTL_PERIPH_{ch['timer']}, "
                f"TIMER_{ch['channel']}, {ch['frequency']} }},"
            )
        lines.append("};")
        lines.append("const bsp_pwm_config_t BOARD_PWM_CFG = {")
        lines.append("    .channels = board_pwm_channels,")
        lines.append(f"    .channel_count = {len(pwm_channels)},")
        lines.append(f"    .clock_hz = {clock},")
        lines.append("};")
        lines.append("")

    qei_encoders = [
        enc for enc in modules.get("encoder", {}).get("encoder", []) if enc.get("interface") == "qei"
    ]
    if qei_encoders:
        lines.append("static const bsp_qei_channel_t board_qei_channels[] = {")
        for enc in qei_encoders:
            lines.append(f"    {{ {enc['qei']}_BASE, SYSCTL_PERIPH_{enc['qei']} }},")
        lines.append("};")
        lines.append("const bsp_qei_config_t BOARD_QEI_CFG = {")
        lines.append("    .channels = board_qei_channels,")
        lines.append(f"    .channel_count = {len(qei_encoders)},")
        lines.append("};")
        lines.append("")

    uart = modules.get("uart_bt", {})
    for u in uart.get("uart", []):
        lines += [
            f"const bsp_uart_config_t BOARD_UART_BT_CFG = {{ {u['module']}_BASE, {u['baud']} }};",
            "",
        ]

    dbg = modules.get("uart_debug", {})
    for d in dbg.get("uart_debug", []):
        lines += [
            f"const bsp_uart_config_t BOARD_UART_DEBUG_CFG = {{ {d['module']}_BASE, {d['baud']} }};",
            "",
        ]

    i2c = modules.get("i2c", {})
    for item in i2c.get("i2c", []):
        lines += [
            f"const bsp_i2c_config_t BOARD_I2C_CFG = {{ {item['module']}_BASE, {item['speed']} }};",
            "",
        ]

    adc_items = modules.get("adc", {}).get("adc", [])
    if adc_items:
        adc_module = adc_items[0]["module"]
        lines.append("static const bsp_adc_channel_t board_adc_channels[] = {")
        for step, item in enumerate(adc_items):
            lines.append(f"    {{ {item['channel']}, {step} }},")
        lines.append("};")
        lines += [
            "const bsp_adc_config_t BOARD_ADC_CFG = {",
            f"    .base = {adc_module}_BASE,",
            "    .sequence = 3,",
            "    .channels = board_adc_channels,",
            f"    .channel_count = {len(adc_items)},",
            "};",
            "",
        ]

        battery_items = [x for x in adc_items if x.get("role") == "battery"]
        if battery_items:
            bat = battery_items[0]
            lines += [
                f"static const bsp_adc_channel_t board_battery_channel = {{ {bat['channel']}, 0 }};",
                "const bsp_adc_config_t BOARD_BATTERY_ADC_CFG = {",
                f"    .base = {bat['module']}_BASE,",
                "    .sequence = 2,",
                "    .channels = &board_battery_channel,",
                "    .channel_count = 1,",
                "};",
                "",
            ]

    ssi_items = modules.get("ssi", {}).get("ssi", [])
    for item in ssi_items:
        lines += [
            "const bsp_spi_config_t BOARD_SPI_CFG = {",
            f"    .base = {item['module']}_BASE,",
            f"    .clock_hz = {item['speed']},",
            "    .mode = BSP_SPI_MODE_0,",
            "    .data_bits = 8,",
            "};",
            "",
        ]

    return lines


def gen_board(gpio: dict, modules: dict[str, dict], board: dict, src_dir: Path, header: BoardHeader) -> None:
    labels = pin_label_map(gpio)
    adc_pins = adc_pin_set(modules)
    plan = PinPlanner()
    deferred_plan = PinPlanner()

    for group in BOARD_GPIO_GROUPS:
        for label in gpio["pinout_groups"].get(group, []):
            if label in BOARD_MUX_LABELS:
                continue
            p = labels[label]
            if pin_label_to_str(p) in adc_pins:
                continue
            target = deferred_plan if defer_gpio_at_boot(label, p["port"], p["pin"]) else plan
            target.add_pin(p["port"], p["pin"], p["direction"], p.get("pull"))

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

    body = emit_board_config_defs(modules, board)
    body += ["bool Board_UartDebug_Init(void) {"]
    dbg = modules.get("uart_debug", {})
    dbg_ports: set[str] = set()
    dbg_plan_lines: list[str] = []
    for d in dbg.get("uart_debug", []):
        if d.get("rx"):
            port, pin_num = parse_pin(d["rx"])
            dbg_ports.add(port)
            dbg_plan_lines.append(f"    GPIOPinConfigure({uart_pin_mux(d['rx'], d['module'], 'rx')});")
            dbg_plan_lines.append(f"    GPIOPinTypeUART(GPIO_PORT{port}_BASE, GPIO_PIN_{pin_num});")
        port, pin_num = parse_pin(d["tx"])
        dbg_ports.add(port)
        dbg_plan_lines.append(f"    GPIOPinConfigure({uart_pin_mux(d['tx'], d['module'], 'tx')});")
        dbg_plan_lines.append(f"    GPIOPinTypeUART(GPIO_PORT{port}_BASE, GPIO_PIN_{pin_num});")
    if dbg_ports:
        port_mask = sum(1 << (ord(p) - ord("A")) for p in dbg_ports)
        body.extend(emit_init_guard(f"bsp_gpio_port_enable(0x{port_mask:X}u)"))
    body += dbg_plan_lines
    body.extend(emit_init_guard("bsp_uart_init(&BOARD_UART_DEBUG_CFG)"))
    body += ["    return true;", "}", ""]

    body += ["bool Board_Periph_Init(void) {", *plan.emit_gpio_setup(guarded=True)]

    if modules.get("dma"):
        body.extend(emit_init_guard("bsp_dma_init()"))

    if uart.get("uart"):
        body.extend(emit_init_guard("bsp_uart_init(&BOARD_UART_BT_CFG)"))

    if dbg.get("uart_debug"):
        body.extend(emit_init_guard("bsp_uart_init(&BOARD_UART_DEBUG_CFG)"))

    if i2c.get("i2c"):
        body.extend(emit_init_guard("bsp_i2c_init(&BOARD_I2C_CFG)"))

    if adc.get("adc"):
        body.extend(emit_init_guard("bsp_adc_init(&BOARD_ADC_CFG)"))
        battery_items = [x for x in adc.get("adc", []) if x.get("role") == "battery"]
        if battery_items:
            body.extend(emit_init_guard("bsp_adc_init(&BOARD_BATTERY_ADC_CFG)"))

    if ssi.get("ssi"):
        body.extend(emit_init_guard("bsp_spi_init(&BOARD_SPI_CFG)"))

    body += ["    return true;", "}"]

    if deferred_plan._ports:
        body += [
            "",
            "/**",
            " * HC-SR04 GPIO（PC1=Echo/SWDIO, PC2=Trig）。",
            " * 勿在 Board_Periph_Init 里初始化，否则 J-Link 只能烧录一次。",
            " * 启用超声波前由驱动调用。",
            " */",
            "bool Board_Ultra_Init(void) {",
            *deferred_plan.emit_gpio_setup(guarded=True),
            "    return true;",
            "}",
        ]

    for u in uart.get("uart", []):
        base = u["module"]
        body += [
            "",
            "void UART_Putc(char c) {",
            f"    bsp_uart_putc({base}_BASE, c);",
            "}",
            "void UART_Puts(const char* s) {",
            f"    bsp_uart_puts({base}_BASE, s);",
            "}",
            "int UART_Getc(char *c) {",
            f"    return bsp_uart_getc({base}_BASE, c);",
            "}",
        ]

    for d in dbg.get("uart_debug", []):
        base = d["module"]
        body += [
            "",
            "void UART_Debug_Putc(char c) {",
            f"    bsp_uart_putc({base}_BASE, c);",
            "}",
            "void UART_Debug_Puts(const char* s) {",
            f"    bsp_uart_puts({base}_BASE, s);",
            "}",
            "int UART_Debug_Getc(char *c) {",
            f"    return bsp_uart_getc({base}_BASE, c);",
            "}",
        ]

    protos = ["bool Board_UartDebug_Init(void);", "bool Board_Periph_Init(void);"]
    if deferred_plan._ports:
        protos.append("bool Board_Ultra_Init(void);")
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

    gpio_helpers_plan = PinPlanner()
    gpio_helpers_plan.merge_gpio(plan)
    gpio_helpers_plan.merge_gpio(deferred_plan)
    write_module(
        "board",
        INCLUDES_BOARD_BSP,
        body,
        protos,
        src_dir,
        header,
        section_title="Board",
        plan=gpio_helpers_plan,
    )


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
    for name in LEGACY_INC_EXTRA:
        path = inc_dir / name
        if path.exists():
            path.unlink()


def gen_ide_compile_db(paths: dict[str, Path], car_project: str) -> None:
    """Emit per-project build/ide-compile-db.json for IDE navigation."""
    tivaware = tivaware_root()
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
        bsp_src / "bsp_sysctl.c",
        bsp_src / "bsp_gpio.c",
        bsp_src / "bsp_systick.c",
        bsp_src / "bsp_uart.c",
        bsp_src / "bsp_i2c.c",
        bsp_src / "bsp_adc.c",
        bsp_src / "bsp_timer.c",
        bsp_src / "bsp_qei.c",
        bsp_src / "bsp_dma.c",
        bsp_src / "bsp_spi.c",
        bsp_src / "bsp_dac.c",
        bsp_src / "bsp_bus_lock.c",
        ROOT / "Common/src/device_profile.c",
        ROOT / "Common/src/start.c",
        ROOT / "Common/src/event.c",
        ROOT / "Common/src/log.c",
        freertos / "tasks.c",
        freertos / "queue.c",
        freertos / "list.c",
        freertos / "timers.c",
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
        ROOT / "Common/src/crc32.c",
        ROOT / "Common/src/nvs.c",
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
    board_header = BoardHeader()
    append_gpio_pins_section(board_header, gpio)
    append_periph_bind_section(board_header, modules, board)
    gen_motor(gpio, modules["motor"], board, src_dir, board_header)
    gen_encoder(gpio, modules["encoder"], src_dir, board_header)
    gen_line(gpio, modules, src_dir, board_header)
    gen_board(gpio, modules, board, src_dir, board_header)
    board_header.write(inc_dir)
    print(f"  -> {inc_dir.relative_to(ROOT)}/board.h")
    gen_gpio_allocation_md(gpio, paths["gpio_doc"], args.car_project)
    if args.ide_db:
        gen_ide_compile_db(paths, args.car_project)
    print("Done.")


if __name__ == "__main__":
    main()
