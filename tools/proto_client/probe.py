"""命令行协议探针：HELLO → PING → SUBSCRIBE，打印原始 TX/RX hex。"""

from __future__ import annotations

import argparse
import sys
import time

import serial

import tm_proto as proto

DEFAULT_PORT = "COM26"


def open_port(port: str, baud: int) -> serial.Serial:
    ser = serial.Serial(
        port=port,
        baudrate=baud,
        timeout=0.05,
        write_timeout=2.0,
        rtscts=False,
        dsrdtr=False,
    )
    ser.dtr = True
    ser.rts = False
    ser.reset_input_buffer()
    return ser


def exchange(ser: serial.Serial, parser: proto.FrameParser, seq: int, cmd: int,
             payload: bytes = b"", wait_s: float = 3.0) -> proto.Frame | None:
    tx = proto.encode_frame(cmd, seq, payload)
    print(f"\n>>> TX seq={seq} cmd=0x{cmd:04X} ({len(tx)}B)")
    print(f"    {tx.hex()}")
    ser.write(tx)
    ser.flush()

    deadline = time.monotonic() + wait_s
    raw = bytearray()
    while time.monotonic() < deadline:
        chunk = ser.read(256)
        if chunk:
            raw.extend(chunk)
            print(f"<<< RX +{len(chunk)}B: {chunk.hex()}")
            for frame in parser.feed(chunk):
                print(
                    f"    FRAME cmd=0x{frame.cmd:04X} seq={frame.seq} "
                    f"flags=0x{frame.flags:02X} nak={frame.is_nak} "
                    f"len={len(frame.payload)}"
                )
                if frame.seq == seq:
                    return frame
        time.sleep(0.01)

    if raw:
        print(f"!!! 超时，累计 RX {len(raw)}B")
    else:
        print("!!! 超时，RX 0 字节")
    return None


def main() -> int:
    ap = argparse.ArgumentParser(description="TM4C123 蓝牙协议探针")
    ap.add_argument("-p", "--port", default=DEFAULT_PORT)
    ap.add_argument("-b", "--baud", type=int, default=115200)
    args = ap.parse_args()

    parser = proto.FrameParser()
    try:
        ser = open_port(args.port, args.baud)
    except serial.SerialException as exc:
        print(f"打开 {args.port} 失败: {exc}", file=sys.stderr)
        return 1

    print(f"已打开 {args.port} @ {args.baud}")
    seq = 0

    def next_seq() -> int:
        nonlocal seq
        seq = (seq + 1) & 0xFF
        if seq == 0:
            seq = 1
        return seq

    hello = exchange(ser, parser, next_seq(), int(proto.Cmd.HELLO))
    if hello is None or hello.is_nak:
        print("HELLO 失败")
        ser.close()
        return 2
    info = proto.parse_hello(hello.payload)
    print(f"HELLO ok caps={proto.caps_text(info.caps)}")

    time.sleep(0.3)
    ser.reset_input_buffer()
    parser = proto.FrameParser()

    ping = exchange(ser, parser, next_seq(), int(proto.Cmd.PING))
    if ping is None:
        print("PING 失败 — 第二帧往返不通")
    else:
        print(f"PING ok uptime={proto.parse_ping_rsp(ping.payload)} ms")

    time.sleep(0.2)
    ser.reset_input_buffer()
    parser = proto.FrameParser()

    mask = int(proto.TelChannel.ATTITUDE | proto.TelChannel.LINE_ADC | proto.TelChannel.MOTOR_RPM)
    sub_payload = proto.build_subscribe(
        mask, proto.DEFAULT_SUB_ATT_HZ, 0, proto.DEFAULT_SUB_LINE_HZ, 0,
        proto.DEFAULT_SUB_MOTOR_RPM_HZ,
    )
    sub = exchange(ser, parser, next_seq(), int(proto.Cmd.SUBSCRIBE), sub_payload)
    if sub is None:
        print("SUBSCRIBE 失败")
    elif sub.is_nak:
        print(f"SUBSCRIBE NAK err={sub.err_code}")
    else:
        print("SUBSCRIBE ok")

    time.sleep(0.2)
    ser.reset_input_buffer()
    parser = proto.FrameParser()

    speed_payload = proto.build_set_speed_wheel(1, 100)
    speed = exchange(ser, parser, next_seq(), int(proto.Cmd.SET_SPEED), speed_payload)
    if speed is None:
        print("SET_SPEED 失败（订阅后）")
    elif speed.is_nak:
        print(f"SET_SPEED NAK err={speed.err_code}")
    else:
        print("SET_SPEED ok (M1=100 RPM)")

    stop = exchange(ser, parser, next_seq(), int(proto.Cmd.SPEED_STOP))
    if stop is None:
        print("SPEED_STOP 失败")
    elif stop.is_nak:
        print(f"SPEED_STOP NAK err={stop.err_code}")
    else:
        print("SPEED_STOP ok")

    ser.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
