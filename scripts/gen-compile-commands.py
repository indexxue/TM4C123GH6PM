"""Generate compile_commands.json for IDE go-to-definition (no BOM, forward slashes)."""
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TIVAWARE = Path(r"D:/Ti/TivaWare_C_Series-2.2.0.295")
FREERTOS = TIVAWARE / "third_party/FreeRTOS/Source"
FREERTOS_PORT = FREERTOS / "portable/GCC/ARM_CM4F"
CBB_WS2812 = ROOT / "cbb/ws2812b"
GCC = ROOT / "tools/bin/arm-none-eabi-gcc.exe"

INCLUDES = [
    ROOT / "include",
    ROOT / "Common/inc",
    CBB_WS2812,
    FREERTOS / "include",
    FREERTOS_PORT,
    TIVAWARE,
    TIVAWARE / "inc",
]

FLAGS = [
    "-mcpu=cortex-m4", "-mthumb", "-mfloat-abi=hard", "-mfpu=fpv4-sp-d16",
    "-DTM4C123GH6PM", "-DPART_TM4C123GH6PM",
    "-std=c11", "-Wall", "-Wextra", "-Wpedantic",
    "-ffunction-sections", "-fdata-sections", "-Os", "-g3",
]
for inc in INCLUDES:
    FLAGS.append(f"-I{inc.as_posix()}")

SOURCES = [
    ROOT / "src/startup_tm4c123gh6pm.c",
    ROOT / "src/main.c",
    ROOT / "src/init.c",
    ROOT / "src/app.c",
    ROOT / "src/freertos_hooks.c",
    ROOT / "src/syscalls.c",
    ROOT / "Common/src/pinout.c",
    ROOT / "Common/src/peripheral.c",
    ROOT / "Common/src/type.c",
    ROOT / "Common/src/log.c",
    ROOT / "Common/src/cmd.c",
    ROOT / "Common/src/battery.c",
    ROOT / "Common/src/button.c",
    ROOT / "Common/src/flexible_button.c",
    ROOT / "Common/src/led_scene.c",
    CBB_WS2812 / "ws2812b.c",
    FREERTOS / "tasks.c",
    FREERTOS / "queue.c",
    FREERTOS / "list.c",
    FREERTOS_PORT / "port.c",
    FREERTOS / "portable/MemMang/heap_4.c",
]


def main() -> int:
    gcc = GCC.as_posix()
    if not GCC.exists():
        print(f"[WARN] {GCC} not found, using bare name", file=sys.stderr)
        gcc = "arm-none-eabi-gcc"

    entries = []
    for src in SOURCES:
        if not src.exists():
            print(f"[SKIP] missing {src}", file=sys.stderr)
            continue
        cmd = " ".join([gcc, *FLAGS, "-c", f'"{src.as_posix()}"'])
        entries.append({
            "directory": ROOT.as_posix(),
            "file": src.as_posix(),
            "command": cmd,
        })

    out = ROOT / "compile_commands.json"
    out.write_text(json.dumps(entries, indent=4) + "\n", encoding="utf-8")
    print(f"  -> {out} ({len(entries)} entries)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
