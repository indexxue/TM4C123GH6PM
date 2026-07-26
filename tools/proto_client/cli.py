#!/usr/bin/env python3
"""无头 CLI：HELLO / PING / GET_TELEMETRY / PARAM_READ / 相机舵机与图传入口。"""

from __future__ import annotations

import argparse
import sys
import time
from pathlib import Path

import serial

import tm_proto as proto

SCHEMA_PATH = Path(__file__).with_name("schema.json")


def request(ser: serial.Serial, cmd: int, payload: bytes = b"", seq: int = 1,
            timeout: float = 2.0) -> proto.Frame:
    ser.write(proto.encode_frame(cmd, seq, payload))
    parser = proto.FrameParser()
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        chunk = ser.read(256)
        if not chunk:
            continue
        for frame in parser.feed(chunk):
            if frame.seq == seq:
                return frame
    raise proto.ProtoError(f"超时 cmd=0x{cmd:04X}")


def cmd_hello(ser: serial.Serial) -> None:
    frame = request(ser, int(proto.Cmd.HELLO))
    if frame.is_nak:
        raise proto.ProtoError(f"NAK {proto.err_text(frame.err_code or 0)}")
    info = proto.parse_hello(frame.payload)
    print(f"proto_ver={info.proto_ver} fw={info.fw_version} hw_rev={info.hw_rev} "
          f"caps={proto.caps_text(info.caps)}")


def cmd_ping(ser: serial.Serial) -> None:
    frame = request(ser, int(proto.Cmd.PING))
    if frame.is_nak:
        raise proto.ProtoError(f"NAK {proto.err_text(frame.err_code or 0)}")
    uptime = proto.parse_ping_rsp(frame.payload)
    print(f"uptime_ms={uptime}")


def cmd_telemetry(ser: serial.Serial) -> None:
    frame = request(ser, int(proto.Cmd.GET_TELEMETRY))
    if frame.is_nak:
        raise proto.ProtoError(f"NAK {proto.err_text(frame.err_code or 0)}")
    snap = proto.parse_telemetry(frame.payload)
    print(f"batt={snap.batt_mv}mV/{snap.batt_pct}% att=({snap.att_text()}) "
          f"enc={snap.enc} line={snap.line_adc} uptime={snap.uptime_ms}")


def cmd_param_read(ser: serial.Serial, param_id: int) -> None:
    frame = request(ser, int(proto.Cmd.PARAM_READ), proto.build_param_read(param_id))
    if frame.is_nak:
        raise proto.ProtoError(f"NAK {proto.err_text(frame.err_code or 0)}")
    print(frame.payload.hex())


def _cam_ack(ser: serial.Serial, cmd: int, payload: bytes = b"") -> None:
    frame = request(ser, cmd, payload)
    if frame.is_nak:
        raise proto.ProtoError(f"NAK {proto.err_text(frame.err_code or 0)}")
    print("ok")


def cmd_cam_snapshot(ser: serial.Serial) -> None:
    frame = request(ser, int(proto.Cmd.GET_CAM_SNAPSHOT))
    if frame.is_nak:
        raise proto.ProtoError(f"NAK {proto.err_text(frame.err_code or 0)}")
    snap = proto.parse_cam_snapshot(frame.payload)
    b0 = snap.detect.boxes[0] if snap.detect.boxes else proto.CamBox()
    print(
        f"link={snap.link} peer={snap.peer_role} "
        f"det=valid={int(snap.detect.valid)} n={snap.detect.count} "
        f"{snap.detect.frame_w}x{snap.detect.frame_h} "
        f"box0=({b0.x},{b0.y},w={b0.w},sc={b0.score_u8}) "
        f"servo=valid={int(snap.servo.valid)} "
        f"pan={snap.servo.pan_deg_x100} tilt={snap.servo.tilt_deg_x100}"
    )


def cmd_cam_net(ser: serial.Serial) -> None:
    frame = request(ser, int(proto.Cmd.GET_CAM_NET), timeout=3.0)
    if frame.is_nak:
        raise proto.ProtoError(f"NAK {proto.err_text(frame.err_code or 0)}")
    net = proto.parse_cam_net(frame.payload)
    print(
        f"valid={int(net.valid)} ip={net.ip_text()} port={net.http_port} "
        f"mode={net.wifi_mode} flags=0x{net.flags:02X} "
        f"stream={net.stream_url()}"
    )


def main() -> int:
    parser = argparse.ArgumentParser(description="TM4C123 蓝牙协议 CLI")
    parser.add_argument("port", help="串口，如 COM7")
    parser.add_argument(
        "command",
        choices=[
            "hello", "ping", "telemetry", "param-read",
            "cam-center", "cam-angle", "cam-nudge", "cam-detect",
            "cam-snapshot", "cam-net",
        ],
    )
    parser.add_argument("--baud", type=int, default=115200, help="波特率，默认 115200")
    parser.add_argument("--param-id", type=int, default=5, help="param-read 用")
    parser.add_argument("--ch", type=int, default=0, help="舵机通道 0=pan 1=tilt")
    parser.add_argument("--deg", type=int, default=18000, help="deg_x100（angle）")
    parser.add_argument("--delta", type=int, default=-500, help="delta_x100（nudge）")
    parser.add_argument("--on", type=int, default=1, help="detect enable 0/1")
    args = parser.parse_args()

    try:
        with serial.Serial(args.port, args.baud, timeout=0.5) as ser:
            if args.command == "hello":
                cmd_hello(ser)
            elif args.command == "ping":
                cmd_ping(ser)
            elif args.command == "telemetry":
                cmd_telemetry(ser)
            elif args.command == "param-read":
                cmd_param_read(ser, args.param_id)
            elif args.command == "cam-center":
                _cam_ack(ser, int(proto.Cmd.CAM_SERVO_CENTER))
            elif args.command == "cam-angle":
                _cam_ack(
                    ser,
                    int(proto.Cmd.CAM_SERVO_SET_ANGLE),
                    proto.build_cam_servo_set_angle(args.ch, args.deg),
                )
            elif args.command == "cam-nudge":
                _cam_ack(
                    ser,
                    int(proto.Cmd.CAM_SERVO_NUDGE),
                    proto.build_cam_servo_nudge(args.ch, args.delta),
                )
            elif args.command == "cam-detect":
                _cam_ack(
                    ser,
                    int(proto.Cmd.CAM_DETECT_ENABLE),
                    proto.build_cam_detect_enable(args.on),
                )
            elif args.command == "cam-snapshot":
                cmd_cam_snapshot(ser)
            elif args.command == "cam-net":
                cmd_cam_net(ser)
    except (serial.SerialException, proto.ProtoError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
