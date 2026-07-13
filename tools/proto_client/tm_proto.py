"""
TM4C123 蓝牙二进制协议 — 帧编解码与载荷解析。

规格见 docs/bluetooth-protocol.md
"""

from __future__ import annotations

import struct
from dataclasses import dataclass, field
from enum import IntEnum, IntFlag
from typing import Iterable, Optional

SOF = b"TM"
PROTO_VER = 0x02
PROTO_VER_LEGACY = 0x01
RESPONSE_BIT = 0x8000
HEADER_SIZE = 7  # VER + FLAGS + LEN + CMD + SEQ

PING_INTERVAL_S = 3.0
LINK_TIMEOUT_S = 30.0

# True：HC-05 半双工保守发送（双发 + 较长写间隔）；False：USB 直连等低延迟链路
HC05_SAFE_TX = False
SET_SPEED_QUIET_S_FMT0 = 0.04
SET_SPEED_QUIET_S_FMT_LR = 0.06
SET_SPEED_DUP_TX_INTERVAL_S = 0.04
TELEMETRY_POLL_HZ = 0.0  # 不再周期轮询；姿态/编码器走常驻推送
DEFAULT_SUB_ATT_HZ = 10
DEFAULT_SUB_ENC_HZ = 5
DEFAULT_SUB_LINE_HZ = 5
DEFAULT_SUB_ULTRA_HZ = 5
DEFAULT_SUB_MOTOR_RPM_HZ = 10
DEFAULT_SUB_ANGLE_LOOP_HZ = 10

# 与固件 board.h LINE_SENSOR_COUNT 一致（car-4wd 为 5 路）
LINE_SENSOR_COUNT = 5

# 超声波未连接/未就绪占位（与固件 PROTO_ULTRA_INVALID_MM 一致）
ULTRASONIC_INVALID_MM = 9999


class Flags(IntFlag):
    DIR_DEVICE = 0x01
    NAK = 0x02
    UNSOLICITED = 0x04


class Cmd(IntEnum):
    HELLO = 0x0001
    PING = 0x0002
    GET_TELEMETRY = 0x0010
    SUBSCRIBE = 0x0011
    UNSUBSCRIBE = 0x0012
    TELEMETRY_PUSH = 0x8011
    PARAM_LIST = 0x0020
    PARAM_READ = 0x0021
    PARAM_WRITE = 0x0022
    DRIVE = 0x0030
    DRIVE_STOP = 0x0031
    SET_SPEED = 0x0032
    SPEED_STOP = 0x0033
    SET_ANGLE = 0x0034
    ANGLE_STOP = 0x0035


class Cap(IntFlag):
    TELEMETRY = 1 << 0
    PARAM_RW = 1 << 1
    SUBSCRIBE = 1 << 2
    DRIVE = 1 << 3
    SPEED_LOOP = 1 << 4
    ANGLE_LOOP = 1 << 5


class TelChannel(IntFlag):
    BATT = 1 << 0
    ATTITUDE = 1 << 1
    ENCODER = 1 << 2
    LINE_ADC = 1 << 3
    ULTRASONIC = 1 << 4
    MOTOR_RPM = 1 << 5
    ANGLE_LOOP = 1 << 6


BASE_CHANNEL_MASK = int(TelChannel.ATTITUDE | TelChannel.ENCODER)
OPTIONAL_CHANNEL_MASK = int(
    TelChannel.BATT | TelChannel.LINE_ADC | TelChannel.ULTRASONIC | TelChannel.MOTOR_RPM |
    TelChannel.ANGLE_LOOP
)

CHANNEL_ID_BATTERY = 0
CHANNEL_ID_ATTITUDE = 1
CHANNEL_ID_ENCODER = 2
CHANNEL_ID_LINE_ADC = 3
CHANNEL_ID_ULTRASONIC = 4
CHANNEL_ID_MOTOR_RPM = 5
CHANNEL_ID_ANGLE_LOOP = 6

HW_REV_CAR_4WD_V1 = 0
HW_REV_CAR_2WD_V1 = 1
MOTOR_COUNT_MAX = 4


def motor_count_for_hw_rev(hw_rev: int) -> int:
    """与固件 nvs.h NVS_HW_REV_* 一致。"""
    if hw_rev == HW_REV_CAR_2WD_V1:
        return 2
    return MOTOR_COUNT_MAX


class ErrCode(IntEnum):
    UNKNOWN_CMD = 0x02
    BAD_LEN = 0x03
    PARAM_ID_INVALID = 0x04
    PARAM_READ_ONLY = 0x05
    PARAM_VALUE_INVALID = 0x06
    NVS_WRITE_FAIL = 0x07
    BUSY = 0x08
    UNSUPPORTED = 0x09


ERR_NAMES = {
    int(ErrCode.UNKNOWN_CMD): "未知命令",
    int(ErrCode.BAD_LEN): "载荷长度错误",
    int(ErrCode.PARAM_ID_INVALID): "参数 ID 无效",
    int(ErrCode.PARAM_READ_ONLY): "参数只读",
    int(ErrCode.PARAM_VALUE_INVALID): "参数值无效",
    int(ErrCode.NVS_WRITE_FAIL): "NVS 写入失败",
    int(ErrCode.BUSY): "设备忙",
    int(ErrCode.UNSUPPORTED): "不支持",
}


class ProtoError(Exception):
    def __init__(self, message: str, code: Optional[int] = None):
        super().__init__(message)
        self.code = code


def crc16_ccitt_false(data: bytes) -> int:
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc


def _response_cmd(cmd: int) -> int:
    if cmd == int(Cmd.TELEMETRY_PUSH):
        return cmd
    return cmd | RESPONSE_BIT


@dataclass
class Frame:
    cmd: int
    seq: int
    flags: int = 0
    payload: bytes = b""
    ver: int = PROTO_VER

    @property
    def is_nak(self) -> bool:
        return bool(self.flags & Flags.NAK)

    @property
    def is_unsolicited(self) -> bool:
        return bool(self.flags & Flags.UNSOLICITED)

    @property
    def err_code(self) -> Optional[int]:
        if self.is_nak and len(self.payload) >= 1:
            return self.payload[0]
        return None


@dataclass
class HelloInfo:
    proto_ver: int
    fw_version: str
    hw_rev: int
    caps: int


@dataclass
class TelemetrySnapshot:
    batt_mv: int
    batt_pct: int
    roll: float
    pitch: float
    yaw: float
    enc: tuple[int, int, int, int]
    line_adc: tuple[int, ...]
    uptime_ms: int
    distance_mm: int = ULTRASONIC_INVALID_MM

    def att_text(self) -> str:
        """仪表盘用整数度展示；三轴均为 -180~180。"""
        return (
            f"roll={int(round(self.roll))}  "
            f"pitch={int(round(self.pitch))}  "
            f"yaw={int(round(self.yaw))}"
        )


@dataclass
class TelemetryPush:
    channel_id: int
    uptime_ms: int
    payload: bytes


@dataclass
class ParamListEntry:
    param_id: int
    writable: bool
    size: int


@dataclass
class FrameParser:
    """增量字节流解析器。"""

    _buf: bytearray = field(default_factory=bytearray)

    def feed(self, data: bytes) -> list[Frame]:
        self._buf.extend(data)
        frames: list[Frame] = []
        while True:
            frame, consumed = try_decode_one(bytes(self._buf))
            if frame is None:
                if consumed > 0:
                    del self._buf[:consumed]
                break
            del self._buf[:consumed]
            frames.append(frame)
        return frames


def encode_frame(cmd: int, seq: int, payload: bytes = b"", *, from_device: bool = False,
                 nak: bool = False, unsolicited: bool = False) -> bytes:
    flags = 0
    if from_device:
        flags |= int(Flags.DIR_DEVICE)
    if nak:
        flags |= int(Flags.NAK)
    if unsolicited:
        flags |= int(Flags.UNSOLICITED)

    header = struct.pack(
        "<BBHHB",
        PROTO_VER,
        flags,
        len(payload),
        cmd & 0xFFFF,
        seq & 0xFF,
    )
    body = header + payload
    crc = crc16_ccitt_false(body)
    return SOF + body + struct.pack("<H", crc)


def try_decode_one(buf: bytes) -> tuple[Optional[Frame], int]:
    """尝试从 buf 解一帧。返回 (frame, consumed_bytes)。"""
    if len(buf) < 2:
        return None, 0

    start = buf.find(SOF)
    if start < 0:
        return None, len(buf)

    if start > 0:
        return None, start

    if len(buf) < 2 + HEADER_SIZE + 2:
        return None, 0

    ver, flags, length, cmd, seq = struct.unpack_from("<BBHHB", buf, 2)
    total = 2 + HEADER_SIZE + length + 2
    if len(buf) < total:
        return None, 0

    payload = buf[2 + HEADER_SIZE:2 + HEADER_SIZE + length]
    crc_expected = struct.unpack_from("<H", buf, 2 + HEADER_SIZE + length)[0]
    crc_body = buf[2:2 + HEADER_SIZE + length]
    if crc16_ccitt_false(crc_body) != crc_expected:
        return None, 2

    return Frame(cmd=cmd, seq=seq, flags=flags, payload=payload, ver=ver), total


def parse_hello(payload: bytes) -> HelloInfo:
    if len(payload) < 25:
        raise ProtoError("HELLO_RSP 载荷过短")
    proto_ver = payload[0]
    fw_raw = payload[1:17]
    hw_rev, caps = struct.unpack_from("<II", payload, 17)
    fw_version = fw_raw.split(b"\x00", 1)[0].decode("ascii", errors="replace")
    return HelloInfo(proto_ver=proto_ver, fw_version=fw_version, hw_rev=hw_rev, caps=caps)


def parse_ping_rsp(payload: bytes) -> int:
    if len(payload) < 4:
        raise ProtoError("PING_RSP 载荷过短")
    (uptime_ms,) = struct.unpack_from("<I", payload, 0)
    return uptime_ms


def telemetry_payload_size(line_count: int = LINE_SENSOR_COUNT, proto_ver: int = PROTO_VER) -> int:
    """GET_TELEMETRY 快照字节数（与 proto_build_telemetry 一致）。"""
    att_bytes = 6 if proto_ver >= PROTO_VER else 12
    _ = line_count
    return 2 + 1 + att_bytes + (4 * 4) + 1 + 4 + 2


def line_mask_to_tuple(mask: int, line_count: int = LINE_SENSOR_COUNT) -> tuple[int, ...]:
    return tuple((mask >> i) & 1 for i in range(line_count))


def _attitude_unpack_fmt(proto_ver: int) -> str:
    return "hhh" if proto_ver >= PROTO_VER else "fff"


def parse_telemetry(payload: bytes, line_count: int = LINE_SENSOR_COUNT,
                    proto_ver: int = PROTO_VER) -> TelemetrySnapshot:
    att_fmt = _attitude_unpack_fmt(proto_ver)
    att_bytes = 6 if proto_ver >= PROTO_VER else 12
    new_size = 2 + 1 + att_bytes + 16 + 1 + 4 + 2
    old_size = 2 + 1 + att_bytes + 16 + (2 * line_count) + 4 + 2

    if len(payload) >= new_size:
        head_fmt = f"<HB{att_fmt}4iB"
        head_size = struct.calcsize(head_fmt)
        values = struct.unpack_from(head_fmt, payload, 0)
        enc = values[5:9]
        line_mask = values[9]
        (uptime_ms,) = struct.unpack_from("<I", payload, head_size)
        distance_mm = ULTRASONIC_INVALID_MM
        if len(payload) >= head_size + 6:
            (distance_mm,) = struct.unpack_from("<H", payload, head_size + 4)
        return TelemetrySnapshot(
            batt_mv=values[0],
            batt_pct=values[1],
            roll=float(values[2]),
            pitch=float(values[3]),
            yaw=float(values[4]),
            enc=enc,
            line_adc=line_mask_to_tuple(line_mask, line_count),
            uptime_ms=uptime_ms,
            distance_mm=distance_mm,
        )

    if len(payload) >= old_size:
        base_fmt = f"<HB{att_fmt}4i{line_count}HI"
        values = struct.unpack_from(base_fmt, payload, 0)
        enc_base = 5
        line_base = enc_base + 4
        uptime_idx = line_base + line_count
        distance_mm = ULTRASONIC_INVALID_MM
        if len(payload) >= old_size + 2:
            (distance_mm,) = struct.unpack_from("<H", payload, old_size)
        raw_adc = values[line_base:line_base + line_count]
        line_det = tuple(1 if v < 2048 else 0 for v in raw_adc)
        return TelemetrySnapshot(
            batt_mv=values[0],
            batt_pct=values[1],
            roll=float(values[2]),
            pitch=float(values[3]),
            yaw=float(values[4]),
            enc=values[enc_base:enc_base + 4],
            line_adc=line_det,
            uptime_ms=values[uptime_idx],
            distance_mm=distance_mm,
        )

    raise ProtoError(
        f"TELEMETRY 载荷过短: {len(payload)} < {new_size} "
        f"(proto_ver={proto_ver}, line={line_count})"
    )


def format_ultrasonic_mm(distance_mm: int) -> str:
    """超声波距离展示；9999 表示未连接/无效。"""
    if distance_mm >= ULTRASONIC_INVALID_MM:
        return "未连接"
    return f"{distance_mm} mm ({distance_mm // 10} cm)"


def parse_telemetry_push(payload: bytes) -> TelemetryPush:
    if len(payload) < 5:
        raise ProtoError("TELEMETRY_PUSH 载荷过短")
    channel_id = payload[0]
    (uptime_ms,) = struct.unpack_from("<I", payload, 1)
    return TelemetryPush(channel_id=channel_id, uptime_ms=uptime_ms, payload=payload[5:])


def parse_attitude_push(payload: bytes, proto_ver: int = PROTO_VER) -> tuple[float, float, float]:
    if proto_ver >= PROTO_VER:
        if len(payload) < 6:
            raise ProtoError("姿态推送载荷过短 (v2 i16)")
        roll, pitch, yaw = struct.unpack_from("<hhh", payload, 0)
        return float(roll), float(pitch), float(yaw)
    if len(payload) < 12:
        raise ProtoError("姿态推送载荷过短 (v1 f32)")
    return struct.unpack_from("<fff", payload, 0)


def parse_battery_push(payload: bytes) -> tuple[int, int]:
    if len(payload) < 3:
        raise ProtoError("电池推送载荷过短")
    batt_mv, batt_pct = struct.unpack_from("<HB", payload, 0)
    return batt_mv, batt_pct


def parse_line_adc_push(payload: bytes, line_count: int = LINE_SENSOR_COUNT) -> tuple[int, ...]:
    """推送 / 遥测：1 字节位掩码（bit=1 表示检测到线）；兼容旧版 u16×N 原始 ADC。"""
    if len(payload) == 1:
        return line_mask_to_tuple(payload[0], line_count)
    need = 2 * line_count
    if len(payload) < need:
        raise ProtoError(f"循迹 ADC 推送载荷过短: {len(payload)} < {need}")
    return struct.unpack_from(f"<{line_count}H", payload, 0)


def parse_encoder_push(payload: bytes) -> tuple[int, int, int, int]:
    if len(payload) < 16:
        raise ProtoError("编码器推送载荷过短")
    return struct.unpack_from("<4i", payload, 0)


def parse_ultrasonic_push(payload: bytes) -> int:
    if len(payload) < 2:
        raise ProtoError("超声波推送载荷过短")
    (distance_mm,) = struct.unpack_from("<H", payload, 0)
    return distance_mm


def parse_motor_rpm_push(payload: bytes) -> tuple[int, int, int, int]:
    if len(payload) < 16:
        raise ProtoError("电机 RPM 推送载荷过短")
    return struct.unpack_from("<4i", payload, 0)


@dataclass
class AngleLoopPush:
    target_yaw: int
    current_yaw: int
    turn_rpm: int
    base_rpm: int


def parse_angle_loop_push(payload: bytes) -> AngleLoopPush:
    if len(payload) < 12:
        raise ProtoError("角度环推送载荷过短")
    target_yaw, current_yaw, turn_rpm, base_rpm = struct.unpack_from("<hhii", payload, 0)
    return AngleLoopPush(
        target_yaw=target_yaw,
        current_yaw=current_yaw,
        turn_rpm=turn_rpm,
        base_rpm=base_rpm,
    )


def parse_param_list(payload: bytes) -> list[ParamListEntry]:
    entries: list[ParamListEntry] = []
    stride = 8
    for off in range(0, len(payload) - stride + 1, stride):
        param_id, flags, size, _ = struct.unpack_from("<HBBH", payload, off)
        entries.append(ParamListEntry(param_id=param_id, writable=bool(flags & 1), size=size))
    return entries


def build_subscribe(mask: int, hz_att: int = 0, hz_enc: int = 0, hz_line: int = 0,
                    hz_ultra: int = 0, hz_motor_rpm: int = 0,
                    hz_angle_loop: int = 0) -> bytes:
    # mask u32 + 6×u8（第 6 字节为 angle_loop Hz）
    return struct.pack(
        "<IBBBBBB",
        mask,
        hz_att & 0xFF,
        hz_enc & 0xFF,
        hz_line & 0xFF,
        hz_ultra & 0xFF,
        hz_motor_rpm & 0xFF,
        hz_angle_loop & 0xFF,
    )


def build_subscribe_mask_only(mask: int) -> bytes:
    """仅 mask（4B），Hz 用固件默认值；蓝牙链路上更短更不易损坏。"""
    return struct.pack("<I", mask)


def build_unsubscribe(mask: int) -> bytes:
    return struct.pack("<I", mask)


def build_param_read(param_id: int) -> bytes:
    return struct.pack("<H", param_id)


def build_param_write(param_id: int, value: bytes) -> bytes:
    return struct.pack("<H", param_id) + value


def build_drive(throttle: int, steer: int) -> bytes:
    throttle = max(-1000, min(1000, throttle))
    steer = max(-1000, min(1000, steer))
    return struct.pack("<hh", throttle, steer)


def build_set_speed_wheel(motor_id: int, rpm: int) -> bytes:
    return struct.pack("<BBi", 0, motor_id & 0xFF, rpm)


def build_set_speed_lr(left_rpm: int, right_rpm: int) -> bytes:
    return struct.pack("<Bii", 1, left_rpm, right_rpm)


def build_set_speed_four(m1: int, m2: int, m3: int, m4: int) -> bytes:
    return struct.pack("<Biiii", 2, m1, m2, m3, m4)


def build_set_angle(delta_yaw: int, base_rpm: int, max_turn_rpm: int = 0) -> bytes:
    """delta_yaw：相对当前航向的转角 Δθ（度），非绝对方位。"""
    if max_turn_rpm != 0:
        return struct.pack("<Bhii", 1, delta_yaw, base_rpm, max_turn_rpm)
    return struct.pack("<Bhi", 0, delta_yaw, base_rpm)


def caps_text(caps: int) -> str:
    names: list[str] = []
    if caps & Cap.TELEMETRY:
        names.append("TELEMETRY")
    if caps & Cap.PARAM_RW:
        names.append("PARAM_RW")
    if caps & Cap.SUBSCRIBE:
        names.append("SUBSCRIBE")
    if caps & Cap.DRIVE:
        names.append("DRIVE")
    if caps & Cap.SPEED_LOOP:
        names.append("SPEED_LOOP")
    if caps & Cap.ANGLE_LOOP:
        names.append("ANGLE_LOOP")
    return ", ".join(names) if names else "none"


def err_text(code: int) -> str:
    return ERR_NAMES.get(code, f"0x{code:02X}")
