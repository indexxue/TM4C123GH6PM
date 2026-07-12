#!/usr/bin/env python3
"""
蓝牙 UART0 收发冒烟测试（不依赖 GUI）。

用法:
  python bt_smoke_test.py COM12 echo
  python bt_smoke_test.py COM12 tx
  python bt_smoke_test.py COM12 hello

前置（回显测试）:
  1. UART7 调试口连接 PC，115200
  2. 发送: bt echo on
  3. 再运行本脚本 echo 模式
  4. 测完 UART7 发送: bt echo off
"""

from __future__ import annotations

import argparse
import sys
import time

import serial

import tm_proto as proto


def test_echo(port: str, baud: int, timeout: float) -> int:
    payload = b"TM4C123 BT echo test\r\n"
    with serial.Serial(port, baud, timeout=timeout) as ser:
        ser.reset_input_buffer()
        ser.write(payload)
        time.sleep(0.2)
        reply = ser.read(len(payload) + 16)

    print(f"TX ({len(payload)}): {payload!r}")
    print(f"RX ({len(reply)}): {reply!r}")

    if payload in reply:
        print("PASS: echo payload found in reply")
        return 0
    print("FAIL: echo mismatch (is 'bt echo on' sent on UART7?)")
    return 1


def test_tx_listen(port: str, baud: int, listen_s: float) -> int:
    """仅监听 UART0；需先在 UART7 执行 bt tx <text>。"""
    print(f"Listening on {port} for {listen_s:.1f}s ...")
    with serial.Serial(port, baud, timeout=0.2) as ser:
        ser.reset_input_buffer()
        deadline = time.monotonic() + listen_s
        chunks: list[bytes] = []
        while time.monotonic() < deadline:
            data = ser.read(256)
            if data:
                chunks.append(data)
                print(f"RX chunk: {data!r}")

    blob = b"".join(chunks)
    if blob:
        print(f"PASS: received {len(blob)} bytes total")
        return 0
    print("FAIL: no data (try UART7: bt tx hello)")
    return 1


def test_hello(port: str, baud: int, timeout: float) -> int:
    parser = proto.FrameParser()
    seq = 1
    frame_bytes = proto.encode_frame(int(proto.Cmd.HELLO), seq)

    with serial.Serial(port, baud, timeout=timeout) as ser:
        ser.reset_input_buffer()
        ser.write(frame_bytes)
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            chunk = ser.read(256)
            if not chunk:
                continue
            for frame in parser.feed(chunk):
                if frame.seq != seq:
                    continue
                if frame.is_nak:
                    print(f"FAIL: NAK {proto.err_text(frame.err_code or 0)}")
                    return 1
                info = proto.parse_hello(frame.payload)
                print(f"PASS: HELLO_RSP proto={info.proto_ver} fw={info.fw_version} "
                      f"hw_rev={info.hw_rev} caps={proto.caps_text(info.caps)}")
                return 0

    print("FAIL: HELLO timeout (need 'bt echo off' for proto mode)")
    return 1


def main() -> int:
    ap = argparse.ArgumentParser(description="TM4C123 Bluetooth smoke test")
    ap.add_argument("port", help="蓝牙虚拟串口，如 COM12")
    ap.add_argument("mode", choices=["echo", "tx", "hello"], help="测试模式")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--timeout", type=float, default=2.0)
    ap.add_argument("--listen", type=float, default=3.0, help="tx 模式监听秒数")
    args = ap.parse_args()

    try:
        if args.mode == "echo":
            return test_echo(args.port, args.baud, args.timeout)
        if args.mode == "tx":
            return test_tx_listen(args.port, args.baud, args.listen)
        return test_hello(args.port, args.baud, args.timeout)
    except serial.SerialException as exc:
        print(f"serial error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
