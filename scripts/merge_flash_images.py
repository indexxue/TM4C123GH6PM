#!/usr/bin/env python3
"""Merge Boot + APP_A + APP_B binaries into one Intel HEX (and optional BIN).

Layout (see include/flash_layout.h / PARTITION.md):
  bootloader @ 0x00000000  (≤16 KB)
  app        @ 0x00004000  (≤116 KB)
  factory    @ 0x00021000  (≤116 KB)
  NVS        @ 0x0003E000  — not included (left erased / untouched)

Unprogrammed gaps are 0xFF so a full-bin flash does not write NVS.
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

FLASH_BL_BASE = 0x00000000
FLASH_BL_SIZE = 0x00004000
FLASH_APP_A_BASE = 0x00004000
FLASH_APP_A_SIZE = 0x0001D000
FLASH_APP_B_BASE = 0x00021000
FLASH_APP_B_SIZE = 0x0001D000
FLASH_NVS_BASE = 0x0003E000


def _load_bin(path: Path, max_size: int, label: str) -> bytes:
    data = path.read_bytes()
    if len(data) > max_size:
        raise SystemExit(
            f"error: {label} {path.name} is {len(data)} bytes, slot limit {max_size}"
        )
    return data


def _place(image: bytearray, base: int, data: bytes, label: str) -> None:
    end = base + len(data)
    if end > len(image):
        raise SystemExit(f"error: {label} extends past image end (0x{end:X} > 0x{len(image):X})")
    image[base : base + len(data)] = data


def _ihex_line(addr: int, record_type: int, payload: bytes) -> str:
    if addr < 0 or addr > 0xFFFF:
        raise ValueError(f"record addr out of range: 0x{addr:X}")
    length = len(payload)
    checksum = (length + ((addr >> 8) & 0xFF) + (addr & 0xFF) + record_type + sum(payload)) & 0xFF
    checksum = ((~checksum) + 1) & 0xFF
    return f":{length:02X}{addr:04X}{record_type:02X}{payload.hex().upper()}{checksum:02X}"


def write_intel_hex(path: Path, image: bytes, base_addr: int = 0) -> None:
    """Write dense image as Intel HEX with ELAR extended linear address records."""
    lines: list[str] = []
    upper = -1
    offset = 0
    while offset < len(image):
        abs_addr = base_addr + offset
        cur_upper = (abs_addr >> 16) & 0xFFFF
        if cur_upper != upper:
            upper = cur_upper
            lines.append(_ihex_line(0, 0x04, bytes([(upper >> 8) & 0xFF, upper & 0xFF])))
        chunk = image[offset : offset + 16]
        lines.append(_ihex_line(abs_addr & 0xFFFF, 0x00, chunk))
        offset += len(chunk)
    lines.append(_ihex_line(0, 0x01, b""))
    path.write_text("\n".join(lines) + "\n", encoding="ascii")


def merge(
    bootloader: Path,
    app: Path,
    factory: Path,
    out_hex: Path,
    out_bin: Path | None,
) -> int:
    bl = _load_bin(bootloader, FLASH_BL_SIZE, "bootloader")
    ap = _load_bin(app, FLASH_APP_A_SIZE, "app")
    ft = _load_bin(factory, FLASH_APP_B_SIZE, "factory")

    image = bytearray([0xFF] * FLASH_NVS_BASE)
    _place(image, FLASH_BL_BASE, bl, "bootloader")
    _place(image, FLASH_APP_A_BASE, ap, "app")
    _place(image, FLASH_APP_B_BASE, ft, "factory")

    out_hex.parent.mkdir(parents=True, exist_ok=True)
    write_intel_hex(out_hex, bytes(image), 0)
    if out_bin is not None:
        out_bin.parent.mkdir(parents=True, exist_ok=True)
        out_bin.write_bytes(image)

    print(
        f"ok: merged {out_hex.name}"
        f" (boot={len(bl)} app={len(ap)} factory={len(ft)} total={len(image)} bytes to NVS)"
    )
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Merge bootloader+app+factory into one Intel HEX (optional BIN)"
    )
    parser.add_argument("--bootloader", type=Path, required=True)
    parser.add_argument("--app", type=Path, required=True)
    parser.add_argument("--factory", type=Path, required=True)
    parser.add_argument("--out-hex", type=Path, required=True)
    parser.add_argument("--out-bin", type=Path, default=None)
    args = parser.parse_args()

    for p, label in (
        (args.bootloader, "bootloader"),
        (args.app, "app"),
        (args.factory, "factory"),
    ):
        if not p.is_file():
            print(f"error: missing {label}: {p}", file=sys.stderr)
            return 1

    return merge(args.bootloader, args.app, args.factory, args.out_hex, args.out_bin)


if __name__ == "__main__":
    raise SystemExit(main())
