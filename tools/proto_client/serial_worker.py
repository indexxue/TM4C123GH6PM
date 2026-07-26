"""
串口收发线程：帧解析、SEQ 匹配、PING 保活、遥测轮询与订阅。
"""

from __future__ import annotations

import json
import queue
import struct
import threading
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Callable, Optional

import serial
from serial.tools import list_ports

from PySide6.QtCore import QObject, QThread, Signal, Slot

import tm_proto as proto

SCHEMA_PATH = Path(__file__).with_name("schema.json")


def load_schema() -> dict[str, Any]:
    with SCHEMA_PATH.open(encoding="utf-8") as f:
        return json.load(f)


def list_serial_ports() -> list[str]:
    return [p.device for p in list_ports.comports()]


@dataclass
class PendingRequest:
    cmd: int
    seq: int
    event: threading.Event
    sent_at: float = 0.0
    response: Optional[proto.Frame] = None


@dataclass
class ManeuverWait:
    done: bool
    timed_out: bool = False
    value: Optional[int] = None


@dataclass
class NavResult:
    """导航序列结束（含超时截断）；ok 表示流程已走完可接受下一次点击。"""
    ok: bool = True
    command_failed: bool = False
    superseded: bool = False
    nav_id: int = 0
    delta_yaw: int = 0
    dist_mm: int = 0
    turned: bool = False
    drove: bool = False
    angle_done: bool = False
    distance_done: bool = False
    actual_delta_yaw: int = 0
    final_dist_mm: Optional[int] = None


class SerialWorker(QThread):
    log = Signal(str)
    connected_changed = Signal(bool)
    hello_received = Signal(object)
    link_alive_changed = Signal(bool)
    telemetry_received = Signal(object)
    push_received = Signal(object)
    motor_rpm_received = Signal(object)
    angle_loop_received = Signal(object)
    distance_loop_received = Signal(object)
    line_loop_received = Signal(object)
    encoder_counts_received = Signal(object)
    battery_received = Signal(int, int)
    cam_detect_received = Signal(object)
    cam_servo_received = Signal(object)
    cam_net_received = Signal(object)
    optional_subscription_changed = Signal(int)
    param_read_result = Signal(int, bytes)
    param_write_result = Signal(int, bool, str)
    param_list_received = Signal(list)
    navigation_finished = Signal(object)

    def __init__(self, parent: Optional[QObject] = None):
        super().__init__(parent)
        self._port: Optional[str] = None
        self._ser: Optional[serial.Serial] = None
        self._running = False
        self._seq = 0
        self._parser = proto.FrameParser()
        self._pending: dict[int, PendingRequest] = {}
        self._pending_lock = threading.Lock()
        self._cmd_queue: queue.Queue[tuple[int, bytes, Optional[Callable]]] = queue.Queue()
        self._last_pong = 0.0
        self._link_alive = False
        self._hello_info: Optional[proto.HelloInfo] = None
        self._next_ping = 0.0
        self._next_telemetry = 0.0
        self._request_timeout_s = 3.0
        self._subscribed = False
        self._optional_mask = 0
        self._speed_payload: Optional[bytes] = None
        self._speed_attempt = 0
        self._angle_payload: Optional[bytes] = None
        self._angle_attempt = 0
        self._distance_payload: Optional[bytes] = None
        self._distance_attempt = 0
        self._critical_depth = 0
        self._want_connected = False
        self._auto_reconnect = True
        self._baudrate = 115200
        self._reconnect_at = 0.0
        self._reconnect_backoff_s = 1.0
        self._drive_stream_active = False
        self._drive_throttle = 0
        self._drive_steer = 0
        self._drive_next_send = 0.0
        self._last_angle_loop: Optional[proto.AngleLoopPush] = None
        self._last_distance_loop: Optional[proto.DistanceLoopPush] = None
        self._nav_latest_id = 0
        self._nav_latest_payload: Optional[bytes] = None

    @property
    def hello_info(self) -> Optional[proto.HelloInfo]:
        return self._hello_info

    @property
    def optional_mask(self) -> int:
        return self._optional_mask

    def is_connected(self) -> bool:
        return self._ser is not None and self._ser.is_open

    @Slot(str, int)
    def connect_port(self, port: str, baudrate: int = 115200) -> None:
        payload = f"{port}\0{baudrate}".encode("ascii")
        self._cmd_queue.put(("connect", payload, None))

    @Slot()
    def disconnect_port(self) -> None:
        self._cmd_queue.put(("disconnect", b"", None))

    @Slot(int)
    def request_param_read(self, param_id: int) -> None:
        payload = proto.build_param_read(param_id)
        self._cmd_queue.put(("param_read", payload, None))

    @Slot(int, bytes)
    def request_param_write(self, param_id: int, value: bytes) -> None:
        payload = proto.build_param_write(param_id, value)
        self._cmd_queue.put(("param_write", payload, None))

    @Slot()
    def request_param_list(self) -> None:
        self._cmd_queue.put(("param_list", b"", None))

    @Slot(bytes)
    def request_set_speed(self, payload: bytes) -> None:
        self._cmd_queue.put(("set_speed", payload, None))

    @Slot()
    def request_speed_stop(self) -> None:
        self._cmd_queue.put(("speed_stop", b"", None))

    @Slot(bytes)
    def request_set_angle(self, payload: bytes) -> None:
        self._cmd_queue.put(("set_angle", payload, None))

    @Slot()
    def request_angle_stop(self) -> None:
        self._cmd_queue.put(("angle_stop", b"", None))

    @Slot(bytes)
    def request_set_distance(self, payload: bytes) -> None:
        self._cmd_queue.put(("set_distance", payload, None))

    @Slot()
    def request_distance_stop(self) -> None:
        self._cmd_queue.put(("distance_stop", b"", None))

    @Slot(bytes)
    def request_set_line_follow(self, payload: bytes) -> None:
        self._cmd_queue.put(("set_line_follow", payload, None))

    @Slot()
    def request_line_follow_stop(self) -> None:
        self._cmd_queue.put(("line_follow_stop", b"", None))

    @Slot(int)
    def request_calib_yaw(self, ref_yaw: int = 0) -> None:
        self._cmd_queue.put(("calib_yaw", proto.build_calib_yaw(ref_yaw), None))

    @Slot()
    def request_cam_servo_center(self) -> None:
        self._cmd_queue.put(("cam_servo_center", b"", None))

    @Slot(int, int)
    def request_cam_servo_set_angle(self, ch: int, deg_x100: int) -> None:
        self._cmd_queue.put(
            ("cam_servo_set_angle", proto.build_cam_servo_set_angle(ch, deg_x100), None)
        )

    @Slot(int, int)
    def request_cam_servo_nudge(self, ch: int, delta_x100: int) -> None:
        self._cmd_queue.put(
            ("cam_servo_nudge", proto.build_cam_servo_nudge(ch, delta_x100), None)
        )

    @Slot(bool)
    def request_cam_detect_enable(self, on: bool) -> None:
        self._cmd_queue.put(
            ("cam_detect_enable", proto.build_cam_detect_enable(1 if on else 0), None)
        )

    @Slot()
    def request_cam_snapshot(self) -> None:
        self._cmd_queue.put(("cam_snapshot", b"", None))

    @Slot()
    def request_cam_net(self) -> None:
        self._cmd_queue.put(("cam_net", b"", None))

    @Slot(int, int)
    def request_drive(self, throttle: int, steer: int) -> None:
        self._cmd_queue.put(("drive", proto.build_drive(throttle, steer), None))

    @Slot(int, int)
    def request_drive_stream(self, throttle: int, steer: int) -> None:
        """持续遥控：worker 按 DRIVE_STREAM_INTERVAL_S 重发，全 0 时停车。"""
        self._cmd_queue.put(("drive_stream", proto.build_drive(throttle, steer), None))

    @Slot()
    def request_drive_stop(self) -> None:
        self._cmd_queue.put(("drive_stop", b"", None))

    @Slot(int, int, int, int)
    def request_navigate(self, delta_yaw_deg: int, dist_mm: int,
                         max_turn_rpm: int = 80, max_drive_rpm: int = 60) -> None:
        self._nav_latest_id += 1
        self._nav_latest_payload = struct.pack(
            "<hiii",
            int(delta_yaw_deg),
            int(dist_mm),
            int(max_turn_rpm),
            int(max_drive_rpm),
        )
        self._cmd_queue.put(("navigate", b"", None))

    @Slot(int)
    def request_apply_subscription(self, optional_mask: int = 0) -> None:
        self._cmd_queue.put(
            ("apply_subscribe", int(optional_mask & proto.OPTIONAL_CHANNEL_MASK).to_bytes(4, "little"), None)
        )

    def _critical_cmd(self, cmd: int) -> bool:
        return cmd in (
            int(proto.Cmd.DRIVE),
            int(proto.Cmd.DRIVE_STOP),
            int(proto.Cmd.SET_SPEED),
            int(proto.Cmd.SPEED_STOP),
            int(proto.Cmd.SET_ANGLE),
            int(proto.Cmd.ANGLE_STOP),
        int(proto.Cmd.SET_DISTANCE),
        int(proto.Cmd.DISTANCE_STOP),
        int(proto.Cmd.SET_LINE_FOLLOW),
        int(proto.Cmd.LINE_FOLLOW_STOP),
        int(proto.Cmd.CALIB_YAW),
        int(proto.Cmd.SUBSCRIBE),
        )

    def _begin_critical(self) -> None:
        self._critical_depth += 1
        hold_until = time.monotonic() + 30.0
        self._next_ping = hold_until
        self._next_telemetry = hold_until

    def _end_critical(self) -> None:
        if self._critical_depth > 0:
            self._critical_depth -= 1
        if self._critical_depth == 0:
            now = time.monotonic()
            self._next_ping = now
            self._next_telemetry = now

    def _subscribe_plan(self, optional_mask: int) -> tuple:
        """返回 (mask, hz_att, hz_enc, hz_line, hz_ultra, hz_motor, hz_angle, hz_distance, hz_line_loop, hz_cam_det, hz_cam_servo)。"""
        mask = proto.BASE_CHANNEL_MASK | (optional_mask & proto.OPTIONAL_CHANNEL_MASK)
        hz_line = proto.DEFAULT_SUB_LINE_HZ if optional_mask & int(proto.TelChannel.LINE_ADC) else 0
        hz_ultra = proto.DEFAULT_SUB_ULTRA_HZ if optional_mask & int(proto.TelChannel.ULTRASONIC) else 0
        hz_motor = 0
        if optional_mask & int(proto.TelChannel.MOTOR_RPM):
            if self._hello_info and (self._hello_info.caps & int(proto.Cap.SPEED_LOOP)):
                hz_motor = proto.DEFAULT_SUB_MOTOR_RPM_HZ
        hz_angle = 0
        if optional_mask & int(proto.TelChannel.ANGLE_LOOP):
            if self._hello_info and (self._hello_info.caps & int(proto.Cap.ANGLE_LOOP)):
                hz_angle = proto.DEFAULT_SUB_ANGLE_LOOP_HZ
        hz_distance = 0
        if optional_mask & int(proto.TelChannel.DISTANCE_LOOP):
            if self._hello_info and (self._hello_info.caps & int(proto.Cap.DISTANCE_LOOP)):
                hz_distance = proto.DEFAULT_SUB_DISTANCE_LOOP_HZ
        hz_line_loop = 0
        if optional_mask & int(proto.TelChannel.LINE_LOOP):
            if self._hello_info and (self._hello_info.caps & int(proto.Cap.LINE_FOLLOW)):
                hz_line_loop = proto.DEFAULT_SUB_LINE_LOOP_HZ
        hz_cam_det = 0
        if optional_mask & int(proto.TelChannel.CAM_DETECT):
            if self._hello_info and (self._hello_info.caps & int(proto.Cap.CAMERA)):
                hz_cam_det = proto.DEFAULT_SUB_CAM_DETECT_HZ
        hz_cam_servo = 0
        if optional_mask & int(proto.TelChannel.CAM_SERVO):
            if self._hello_info and (self._hello_info.caps & int(proto.Cap.CAMERA)):
                hz_cam_servo = proto.DEFAULT_SUB_CAM_SERVO_HZ
        return (
            mask,
            proto.DEFAULT_SUB_ATT_HZ,
            proto.DEFAULT_SUB_ENC_HZ,
            hz_line,
            hz_ultra,
            hz_motor,
            hz_angle,
            hz_distance,
            hz_line_loop,
            hz_cam_det,
            hz_cam_servo,
        )

    def _unsubscribe_optional(self) -> bool:
        if not self.is_connected() or self._optional_mask == 0:
            return True
        self._flush_serial_input()
        self._wait_rx_quiet(quiet_s=0.1, timeout_s=0.8)
        frame = self._send_request_retry(
            int(proto.Cmd.UNSUBSCRIBE),
            proto.build_unsubscribe(proto.OPTIONAL_CHANNEL_MASK),
            timeout=3.0,
            retries=2,
        )
        self._flush_serial_input()
        self._wait_rx_quiet(quiet_s=0.12, timeout_s=1.0)
        return frame is not None and not frame.is_nak

    def _pause_pushes(self) -> bool:
        if not self.is_connected():
            return True
        ok = self._unsubscribe_optional()
        if not ok and self._optional_mask:
            self.log.emit("UNSUBSCRIBE 可选通道未确认")
        return ok

    def _apply_subscription_now(self, optional_mask: Optional[int] = None) -> None:
        if optional_mask is not None:
            self._optional_mask = optional_mask & proto.OPTIONAL_CHANNEL_MASK
        if not self.is_connected():
            return
        if self._hello_info is None or not (self._hello_info.caps & int(proto.Cap.SUBSCRIBE)):
            return
        mask, hz_att, hz_enc, hz_line, hz_ultra, hz_motor, hz_angle, hz_distance, hz_line_loop, hz_cam_det, hz_cam_servo = (
            self._subscribe_plan(self._optional_mask)
        )
        self._wait_rx_quiet()
        frame = self._send_request_retry(
            int(proto.Cmd.SUBSCRIBE),
            proto.build_subscribe(
                mask,
                hz_att=hz_att,
                hz_enc=hz_enc,
                hz_line=hz_line,
                hz_ultra=hz_ultra,
                hz_motor_rpm=hz_motor,
                hz_angle_loop=hz_angle,
                hz_distance_loop=hz_distance,
                hz_line_loop=hz_line_loop,
                hz_cam_detect=hz_cam_det,
                hz_cam_servo=hz_cam_servo,
            ),
            timeout=3.0,
            retries=2,
        )
        if frame is None or frame.is_nak:
            return
        self._subscribed = True
        parts = [f"常驻 姿态@{hz_att}Hz", f"编码器@{hz_enc}Hz"]
        if self._optional_mask & int(proto.TelChannel.BATT):
            parts.append("电量@1Hz")
        if self._optional_mask & int(proto.TelChannel.LINE_ADC):
            parts.append(f"循迹@{hz_line}Hz")
        if self._optional_mask & int(proto.TelChannel.ULTRASONIC):
            parts.append(f"超声@{hz_ultra}Hz")
        if self._optional_mask & int(proto.TelChannel.MOTOR_RPM) and hz_motor:
            parts.append(f"RPM@{hz_motor}Hz")
        if self._optional_mask & int(proto.TelChannel.ANGLE_LOOP) and hz_angle:
            parts.append(f"角度@{hz_angle}Hz")
        if self._optional_mask & int(proto.TelChannel.DISTANCE_LOOP) and hz_distance:
            parts.append(f"距离@{hz_distance}Hz")
        if self._optional_mask & int(proto.TelChannel.LINE_LOOP) and hz_line_loop:
            parts.append(f"循迹环@{hz_line_loop}Hz")
        if self._optional_mask & int(proto.TelChannel.CAM_DETECT) and hz_cam_det:
            parts.append(f"检测@{hz_cam_det}Hz")
        if self._optional_mask & int(proto.TelChannel.CAM_SERVO) and hz_cam_servo:
            parts.append(f"云台@{hz_cam_servo}Hz")
        self.log.emit(f"SUBSCRIBE ok ({' + '.join(parts)})")
        self.optional_subscription_changed.emit(self._optional_mask)

    def run(self) -> None:
        self._running = True

        while self._running:
            self._drain_commands()
            now = time.monotonic()

            if not self.is_connected():
                if self._link_alive:
                    self._set_link_alive(False)
                if self._want_connected and self._auto_reconnect and self._port:
                    if now >= self._reconnect_at:
                        self.log.emit(f"重连 {self._port} @ {self._baudrate}...")
                        self._open_serial(self._port, self._baudrate)
                        if not self.is_connected():
                            self._reconnect_at = now + self._reconnect_backoff_s
                            self._reconnect_backoff_s = min(self._reconnect_backoff_s * 1.5, 15.0)
                        else:
                            self._reconnect_backoff_s = 1.0
                time.sleep(0.05)
                continue

            if self._critical_depth == 0 and now >= self._next_ping:
                if not self._has_pending_cmd(int(proto.Cmd.PING)):
                    self._post_request(int(proto.Cmd.PING))
                self._next_ping = now + proto.PING_INTERVAL_S

            if (
                self._drive_stream_active
                and self._critical_depth == 0
                and now >= self._drive_next_send
            ):
                self._send_drive_stream_frame()
                self._drive_next_send = now + proto.DRIVE_STREAM_INTERVAL_S

            if (
                self._critical_depth == 0
                and proto.TELEMETRY_POLL_HZ > 0
                and now >= self._next_telemetry
            ):
                if not self._has_pending_cmd(int(proto.Cmd.GET_TELEMETRY)):
                    self._post_request(int(proto.Cmd.GET_TELEMETRY))
                self._next_telemetry = now + (1.0 / proto.TELEMETRY_POLL_HZ)

            if self._link_alive and (now - self._last_pong) > proto.LINK_TIMEOUT_S:
                self.log.emit("链路超时，准备重连...")
                self._schedule_reconnect()
                continue

            self._pump_serial()
            time.sleep(0.005)

        self._close_serial()

    def stop(self) -> None:
        self._running = False
        self.wait(3000)

    def _drain_commands(self) -> None:
        while True:
            try:
                kind, payload, _ = self._cmd_queue.get_nowait()
            except queue.Empty:
                break

            if kind == "connect":
                port, baud_text = payload.decode("ascii").split("\0", 1)
                baudrate = int(baud_text)
                self._want_connected = True
                self._port = port
                self._baudrate = baudrate
                self._reconnect_backoff_s = 1.0
                self._reconnect_at = 0.0
                self._open_serial(port, baudrate)
            elif kind == "disconnect":
                self._want_connected = False
                self._close_serial()
                self.connected_changed.emit(False)
                self._set_link_alive(False)
            elif kind == "param_read":
                if self.is_connected():
                    param_id = int.from_bytes(payload[:2], "little")
                    self._post_request(
                        int(proto.Cmd.PARAM_READ),
                        payload,
                        on_response=lambda f, pid=param_id: self._on_param_read(pid, f),
                    )
            elif kind == "param_write":
                if self.is_connected():
                    param_id = int.from_bytes(payload[:2], "little")
                    self._post_request(
                        int(proto.Cmd.PARAM_WRITE),
                        payload,
                        on_response=lambda f, pid=param_id: self._on_param_write(pid, f),
                    )
            elif kind == "param_list":
                if self.is_connected():
                    self._post_request(
                        int(proto.Cmd.PARAM_LIST),
                        b"",
                        on_response=self._on_param_list,
                    )
            elif kind == "apply_subscribe":
                if self.is_connected():
                    optional = int.from_bytes(payload[:4], "little", signed=False)
                    self._apply_subscription_now(optional)
            elif kind == "set_speed":
                if self.is_connected():
                    self._speed_payload = payload
                    self._speed_attempt = 0
                    self._start_set_speed_attempt()
            elif kind == "set_speed_retry":
                if self.is_connected():
                    self._start_set_speed_attempt()
            elif kind == "speed_stop":
                if self.is_connected():
                    self._post_speed_stop()
            elif kind == "set_angle":
                if self.is_connected():
                    self._angle_payload = payload
                    self._angle_attempt = 0
                    self._start_set_angle_attempt()
            elif kind == "set_angle_retry":
                if self.is_connected():
                    self._start_set_angle_attempt()
            elif kind == "angle_stop":
                if self.is_connected():
                    self._post_angle_stop()
            elif kind == "set_distance":
                if self.is_connected():
                    self._distance_payload = payload
                    self._distance_attempt = 0
                    self._start_set_distance_attempt()
            elif kind == "set_distance_retry":
                if self.is_connected():
                    self._start_set_distance_attempt()
            elif kind == "distance_stop":
                if self.is_connected():
                    self._post_distance_stop()
            elif kind == "set_line_follow":
                if self.is_connected():
                    frame = self._send_request_retry(
                        int(proto.Cmd.SET_LINE_FOLLOW),
                        payload,
                        timeout=2.0,
                        retries=2,
                    )
                    if frame is None or frame.is_nak:
                        self.log.emit("SET_LINE_FOLLOW 失败")
                    else:
                        self.log.emit("SET_LINE_FOLLOW ok")
            elif kind == "line_follow_stop":
                if self.is_connected():
                    frame = self._send_request_retry(
                        int(proto.Cmd.LINE_FOLLOW_STOP),
                        b"",
                        timeout=2.0,
                        retries=2,
                    )
                    if frame is None or frame.is_nak:
                        self.log.emit("LINE_FOLLOW_STOP 失败")
                    else:
                        self.log.emit("LINE_FOLLOW_STOP ok")
            elif kind == "calib_yaw":
                if self.is_connected():
                    self._run_calib_yaw(payload)
            elif kind == "cam_servo_center":
                if self.is_connected():
                    frame = self._send_request_retry(
                        int(proto.Cmd.CAM_SERVO_CENTER), b"", timeout=2.0, retries=2
                    )
                    self.log.emit(
                        "CAM_SERVO_CENTER ok"
                        if frame and not frame.is_nak
                        else "CAM_SERVO_CENTER 失败"
                    )
            elif kind == "cam_servo_set_angle":
                if self.is_connected():
                    frame = self._send_request_retry(
                        int(proto.Cmd.CAM_SERVO_SET_ANGLE), payload, timeout=2.0, retries=2
                    )
                    self.log.emit(
                        "CAM_SERVO_SET_ANGLE ok"
                        if frame and not frame.is_nak
                        else "CAM_SERVO_SET_ANGLE 失败"
                    )
            elif kind == "cam_servo_nudge":
                if self.is_connected():
                    frame = self._send_request_retry(
                        int(proto.Cmd.CAM_SERVO_NUDGE), payload, timeout=2.0, retries=2
                    )
                    self.log.emit(
                        "CAM_SERVO_NUDGE ok"
                        if frame and not frame.is_nak
                        else "CAM_SERVO_NUDGE 失败"
                    )
            elif kind == "cam_detect_enable":
                if self.is_connected():
                    frame = self._send_request_retry(
                        int(proto.Cmd.CAM_DETECT_ENABLE), payload, timeout=2.0, retries=2
                    )
                    self.log.emit(
                        "CAM_DETECT_ENABLE ok"
                        if frame and not frame.is_nak
                        else "CAM_DETECT_ENABLE 失败"
                    )
            elif kind == "cam_snapshot":
                if self.is_connected():
                    frame = self._send_request_retry(
                        int(proto.Cmd.GET_CAM_SNAPSHOT), b"", timeout=2.0, retries=2
                    )
                    if frame and not frame.is_nak:
                        try:
                            snap = proto.parse_cam_snapshot(frame.payload)
                            self.cam_detect_received.emit(snap.detect)
                            self.cam_servo_received.emit(snap.servo)
                            self.log.emit(
                                f"CAM_SNAPSHOT link={snap.link} "
                                f"det={snap.detect.count} "
                                f"pan={snap.servo.pan_deg_x100}"
                            )
                        except proto.ProtoError as exc:
                            self.log.emit(f"CAM_SNAPSHOT 解析失败: {exc}")
                    else:
                        self.log.emit("CAM_SNAPSHOT 失败")
            elif kind == "cam_net":
                if self.is_connected():
                    frame = self._send_request_retry(
                        int(proto.Cmd.GET_CAM_NET), b"", timeout=3.0, retries=2
                    )
                    if frame and not frame.is_nak:
                        try:
                            net = proto.parse_cam_net(frame.payload)
                            self.cam_net_received.emit(net)
                            self.log.emit(f"CAM_NET {net.stream_url()}")
                        except proto.ProtoError as exc:
                            self.log.emit(f"CAM_NET 解析失败: {exc}")
                    else:
                        self.log.emit("CAM_NET 失败")
            elif kind == "drive":
                if self.is_connected():
                    frame = self._send_request_retry(
                        int(proto.Cmd.DRIVE),
                        payload,
                    )
                    self._on_drive_rsp(frame)
            elif kind == "drive_stream":
                if self.is_connected():
                    self._apply_drive_stream(payload)
            elif kind == "drive_stop":
                if self.is_connected():
                    frame = self._send_request_retry(
                        int(proto.Cmd.DRIVE_STOP),
                        b"",
                    )
                    self._on_drive_stop_rsp(frame)
            elif kind == "navigate":
                if self.is_connected():
                    self._process_navigate_chain()

    def _flush_serial_input(self) -> None:
        if self._ser is None:
            return
        try:
            self._ser.reset_input_buffer()
        except serial.SerialException:
            pass
        self._parser = proto.FrameParser()

    def _subscribe_once(self, sub_payload: bytes) -> Optional[proto.Frame]:
        frame = self._send_request(int(proto.Cmd.SUBSCRIBE), sub_payload, timeout=3.0)
        if frame is not None:
            return frame
        pending = bytes(self._parser._buf)
        waiting = 0
        if self._ser is not None:
            try:
                waiting = self._ser.in_waiting
            except serial.SerialException:
                pass
        extra = ""
        if pending or waiting:
            extra = f" | 缓冲={pending.hex()}"
            if waiting:
                extra += f" in_waiting={waiting}"
        self.log.emit(f"SUBSCRIBE 无应答{extra}")
        return None

    def _has_pending_cmd(self, cmd: int) -> bool:
        with self._pending_lock:
            return any(pending.cmd == cmd for pending in self._pending.values())

    def _pump_serial(self, raw_log: Optional[list[bytes]] = None) -> None:
        if not self.is_connected():
            return

        try:
            chunk = self._ser.read(256)  # type: ignore[union-attr]
        except serial.SerialException as exc:
            self.log.emit(f"读取失败: {exc}")
            self._schedule_reconnect()
            return

        if not chunk:
            return

        if raw_log is not None:
            raw_log.append(chunk)

        for frame in self._parser.feed(chunk):
            self._handle_frame(frame)

    def _write_frame(self, frame_bytes: bytes) -> bool:
        try:
            written = self._ser.write(frame_bytes)  # type: ignore[union-attr]
            self._ser.flush()  # type: ignore[union-attr]
        except serial.SerialException as exc:
            self.log.emit(f"发送失败: {exc}")
            return False
        if written != len(frame_bytes):
            self.log.emit(f"发送不完整 {written}/{len(frame_bytes)} B")
            return False
        if len(frame_bytes) >= 7:
            plen = frame_bytes[4] | (frame_bytes[5] << 8)
            if plen > 0:
                if proto.HC05_SAFE_TX:
                    time.sleep(0.025 + min(plen, 32) * 0.004)
                else:
                    time.sleep(0.008 + min(plen, 32) * 0.0015)
        return True

    def _wait_rx_quiet(self, quiet_s: float = 0.06, timeout_s: float = 0.6) -> None:
        if not self.is_connected():
            return
        deadline = time.monotonic() + timeout_s
        last_rx = time.monotonic()
        while time.monotonic() < deadline:
            self._pump_serial()
            try:
                waiting = self._ser.in_waiting  # type: ignore[union-attr]
            except serial.SerialException:
                waiting = 0
            if waiting > 0:
                last_rx = time.monotonic()
            elif time.monotonic() - last_rx >= quiet_s:
                return
            time.sleep(0.005)

    def _send_request_retry(
        self,
        cmd: int,
        payload: bytes = b"",
        timeout: Optional[float] = None,
        retries: int = 3,
    ) -> Optional[proto.Frame]:
        if timeout is None:
            timeout = self._request_timeout_s
        for attempt in range(retries):
            if attempt > 0:
                time.sleep(0.1)
                self._flush_serial_input()
            self._wait_rx_quiet()
            frame = self._send_request(
                cmd, payload, timeout=timeout, duplicate_tx=self._critical_cmd(cmd)
            )
            if frame is not None:
                return frame
        return None

    def _schedule_reconnect(self) -> None:
        self._close_serial()
        self._set_link_alive(False)
        if self._want_connected and self._port:
            self._reconnect_at = time.monotonic() + 0.5
        else:
            self.connected_changed.emit(False)

    def _touch_link(self) -> None:
        self._last_pong = time.monotonic()
        self._set_link_alive(True)

    def _open_serial(self, port: str, baudrate: int = 115200) -> None:
        self._close_serial()
        try:
            self._ser = serial.Serial(
                port=port,
                baudrate=baudrate,
                timeout=0.05,
                write_timeout=2.0,
                rtscts=False,
                dsrdtr=False,
            )
            # HC-05 常见：拉高 DTR 进入透传
            self._ser.dtr = True
            self._ser.rts = False
        except serial.SerialException as exc:
            self.log.emit(f"打开串口失败: {exc}")
            if self._want_connected:
                self._reconnect_at = time.monotonic() + self._reconnect_backoff_s
                self._reconnect_backoff_s = min(self._reconnect_backoff_s * 1.5, 15.0)
            return

        self._port = port
        self._parser = proto.FrameParser()
        self._seq = 0
        self._last_pong = time.monotonic()
        self._hello_info = None
        self._subscribed = False
        self._optional_mask = 0
        self._critical_depth = 0
        self._nav_latest_id = 0
        self._nav_latest_payload = None
        self.log.emit(f"已连接 {port} @ {baudrate}")
        self.connected_changed.emit(True)

        frame = self._send_request(int(proto.Cmd.HELLO), timeout=2.0)
        if frame is None or frame.is_nak:
            self.log.emit("HELLO 失败")
            self._close_serial()
            if self._want_connected:
                self._reconnect_at = time.monotonic() + self._reconnect_backoff_s
                self._reconnect_backoff_s = min(self._reconnect_backoff_s * 1.5, 15.0)
            else:
                self.connected_changed.emit(False)
            return

        try:
            self._hello_info = proto.parse_hello(frame.payload)
            self.hello_received.emit(self._hello_info)
            self.log.emit(
                f"HELLO ok proto={self._hello_info.proto_ver} "
                f"fw={self._hello_info.fw_version} caps={proto.caps_text(self._hello_info.caps)}"
            )
        except proto.ProtoError as exc:
            self.log.emit(f"HELLO 解析失败: {exc}")
            self._close_serial()
            if self._want_connected:
                self._reconnect_at = time.monotonic() + self._reconnect_backoff_s
                self._reconnect_backoff_s = min(self._reconnect_backoff_s * 1.5, 15.0)
            else:
                self.connected_changed.emit(False)
            return

        self._flush_serial_input()
        time.sleep(0.5)

        ping_frame = self._send_request(int(proto.Cmd.PING), timeout=2.0)
        if ping_frame is None:
            self.log.emit("PING 无应答 — 第二帧往返失败（HELLO 后链路异常）")
        else:
            try:
                uptime = proto.parse_ping_rsp(ping_frame.payload)
                self.log.emit(f"PING ok uptime={uptime} ms")
            except proto.ProtoError as exc:
                self.log.emit(f"PING 应答解析失败: {exc}")

        if self._hello_info.caps & int(proto.Cap.SUBSCRIBE):
            self.log.emit("连接就绪；姿态+编码器固件常驻推送，其它通道在仪表盘订阅")
            if baudrate >= 115200:
                self.log.emit("提示: 若 SET_SPEED 仍超时，可尝试波特率 57600 或 9600")
            optional = 0
            if self._hello_info.caps & int(proto.Cap.SPEED_LOOP):
                optional |= int(proto.TelChannel.MOTOR_RPM)
            if self._hello_info.caps & int(proto.Cap.ANGLE_LOOP):
                optional |= int(proto.TelChannel.ANGLE_LOOP)
            if self._hello_info.caps & int(proto.Cap.DISTANCE_LOOP):
                optional |= int(proto.TelChannel.DISTANCE_LOOP)
            self._apply_subscription_now(optional)

        if ping_frame is not None:
            self._touch_link()
        else:
            self.log.emit("警告: 仅 HELLO 成功，后续命令无应答")
            self._touch_link()
        now = time.monotonic()
        self._reconnect_backoff_s = 1.0
        self._next_ping = now
        self._next_telemetry = now + 3600.0

    def _close_serial(self) -> None:
        self._drive_stream_active = False
        self._drive_throttle = 0
        self._drive_steer = 0
        if self._ser is not None:
            try:
                self._ser.close()
            except serial.SerialException:
                pass
        self._ser = None
        self._port = None
        with self._pending_lock:
            for pending in self._pending.values():
                pending.response = None
                pending.event.set()
            self._pending.clear()

    def _next_seq(self) -> int:
        self._seq = (self._seq + 1) & 0xFF
        if self._seq == 0:
            self._seq = 1
        return self._seq

    def _expected_response_cmd(self, req_cmd: int) -> int:
        # SUBSCRIBE 应答 cmd = SUBSCRIBE|RESPONSE_BIT，与 TELEMETRY_PUSH 同为 0x8011
        return proto._response_cmd(req_cmd)

    def _post_request(self, cmd: int, payload: bytes = b"",
                      on_response: Optional[Callable[[proto.Frame], None]] = None,
                      timeout: Optional[float] = None,
                      duplicate_tx: bool = False) -> Optional[int]:
        if not self.is_connected():
            return None

        seq = self._next_seq()
        frame_bytes = proto.encode_frame(cmd, seq, payload)
        pending = PendingRequest(cmd=cmd, seq=seq, event=threading.Event(),
                                 sent_at=time.monotonic())

        with self._pending_lock:
            self._pending[seq] = pending

        try:
            if not self._write_frame(frame_bytes):
                with self._pending_lock:
                    self._pending.pop(seq, None)
                return None
            if duplicate_tx and proto.HC05_SAFE_TX:
                time.sleep(proto.SET_SPEED_DUP_TX_INTERVAL_S)
                if not self._write_frame(frame_bytes):
                    with self._pending_lock:
                        self._pending.pop(seq, None)
                    return None
        except serial.SerialException as exc:
            self.log.emit(f"发送失败: {exc}")
            with self._pending_lock:
                self._pending.pop(seq, None)
            return None

        if on_response is not None:
            req_timeout = timeout if timeout is not None else self._request_timeout_s

            def _wait_and_dispatch() -> None:
                deadline = time.monotonic() + req_timeout
                while not pending.event.is_set():
                    if time.monotonic() >= deadline:
                        with self._pending_lock:
                            self._pending.pop(seq, None)
                        on_response(None)
                        return
                    pending.event.wait(0.02)
                on_response(pending.response)

            threading.Thread(target=_wait_and_dispatch, daemon=True).start()
        return seq

    def _send_request(self, cmd: int, payload: bytes = b"", timeout: float = 1.0,
                      duplicate_tx: bool = False,
                      on_response: Optional[Callable[[proto.Frame], None]] = None) -> Optional[proto.Frame]:
        if not self.is_connected():
            return None

        seq = self._next_seq()
        frame_bytes = proto.encode_frame(cmd, seq, payload)
        pending = PendingRequest(cmd=cmd, seq=seq, event=threading.Event(),
                                 sent_at=time.monotonic())

        with self._pending_lock:
            self._pending[seq] = pending

        try:
            if not self._write_frame(frame_bytes):
                with self._pending_lock:
                    self._pending.pop(seq, None)
                return None
            if duplicate_tx and proto.HC05_SAFE_TX:
                time.sleep(proto.SET_SPEED_DUP_TX_INTERVAL_S)
                if not self._write_frame(frame_bytes):
                    with self._pending_lock:
                        self._pending.pop(seq, None)
                    return None
        except serial.SerialException as exc:
            self.log.emit(f"发送失败: {exc}")
            with self._pending_lock:
                self._pending.pop(seq, None)
            return None

        deadline = time.monotonic() + timeout
        raw_chunks: list[bytes] = []
        while not pending.event.is_set():
            if time.monotonic() >= deadline:
                break
            self._pump_serial(raw_chunks)
            pending.event.wait(0.02)

        with self._pending_lock:
            self._pending.pop(seq, None)

        if not pending.event.is_set():
            self.log.emit(f"请求超时 cmd=0x{cmd:04X} seq={seq}")
            raw = b"".join(raw_chunks)
            if raw:
                preview = raw[:64].hex()
                if len(raw) > 64:
                    preview += "..."
                self.log.emit(f"  超时期间 RX {len(raw)}B: {preview}")
            return None

        response = pending.response
        if response is not None and on_response is not None:
            on_response(response)
        return response

    def _maybe_emit_telemetry(self, frame: proto.Frame) -> None:
        if frame.is_nak:
            return
        if frame.cmd != (int(proto.Cmd.GET_TELEMETRY) | proto.RESPONSE_BIT):
            return
        try:
            proto_ver = (
                self._hello_info.proto_ver
                if self._hello_info is not None
                else proto.PROTO_VER
            )
            snap = proto.parse_telemetry(frame.payload, proto_ver=proto_ver)
            self.telemetry_received.emit(snap)
        except proto.ProtoError as exc:
            preview = frame.payload[:24].hex()
            if len(frame.payload) > 24:
                preview += "..."
            proto_ver = (
                self._hello_info.proto_ver
                if self._hello_info is not None
                else proto.PROTO_VER
            )
            self.log.emit(
                f"遥测解析失败: {exc} | 实收 {len(frame.payload)}B 期望 "
                f"{proto.telemetry_payload_size(proto_ver=proto_ver)}B | hex={preview}"
            )

    def _handle_frame(self, frame: proto.Frame) -> None:
        # 仅 solicited 应答可匹配 pending（推送 seq=0 且带 UNSOLICITED）
        pending: Optional[PendingRequest] = None
        if not frame.is_unsolicited:
            with self._pending_lock:
                cand = self._pending.get(frame.seq)
                if cand is not None:
                    exp_cmd = self._expected_response_cmd(cand.cmd)
                    if frame.cmd == exp_cmd:
                        pending = self._pending.pop(frame.seq)

        if pending is not None:
            pending.response = frame
            pending.event.set()
            if not frame.is_nak and (frame.flags & int(proto.Flags.DIR_DEVICE)):
                self._touch_link()
            self._maybe_emit_telemetry(frame)
            return

        if frame.is_unsolicited and frame.cmd == int(proto.Cmd.TELEMETRY_PUSH):
            if not frame.is_nak and (frame.flags & int(proto.Flags.DIR_DEVICE)):
                self._touch_link()
            try:
                push = proto.parse_telemetry_push(frame.payload)
                if push.channel_id == proto.CHANNEL_ID_BATTERY:
                    mv, pct = proto.parse_battery_push(push.payload)
                    self.battery_received.emit(mv, pct)
                elif push.channel_id == proto.CHANNEL_ID_MOTOR_RPM:
                    self.motor_rpm_received.emit(proto.parse_motor_rpm_push(push.payload))
                elif push.channel_id == proto.CHANNEL_ID_ANGLE_LOOP:
                    sample = proto.parse_angle_loop_push(push.payload)
                    self._last_angle_loop = sample
                    self.angle_loop_received.emit(sample)
                elif push.channel_id == proto.CHANNEL_ID_DISTANCE_LOOP:
                    sample = proto.parse_distance_loop_push(push.payload)
                    self._last_distance_loop = sample
                    self.distance_loop_received.emit(sample)
                elif push.channel_id == proto.CHANNEL_ID_LINE_LOOP:
                    self.line_loop_received.emit(proto.parse_line_loop_push(push.payload))
                elif push.channel_id == proto.CHANNEL_ID_ENCODER:
                    self.encoder_counts_received.emit(proto.parse_encoder_push(push.payload))
                elif push.channel_id == proto.CHANNEL_ID_CAM_DETECT:
                    self.cam_detect_received.emit(proto.parse_cam_detect_push(push.payload))
                elif push.channel_id == proto.CHANNEL_ID_CAM_SERVO:
                    self.cam_servo_received.emit(proto.parse_cam_servo_push(push.payload))
                self.push_received.emit(push)
            except proto.ProtoError as exc:
                self.log.emit(f"推送解析失败: {exc}")
            return

        if not frame.is_nak and (frame.flags & int(proto.Flags.DIR_DEVICE)):
            self._touch_link()
            if not frame.is_unsolicited:
                # 双发命令时固件可能回两条相同 seq 的空 ACK，第二条忽略
                if len(frame.payload) == 0 and frame.cmd in (
                    int(proto.Cmd.TELEMETRY_PUSH),
                    int(proto.Cmd.DRIVE) | proto.RESPONSE_BIT,
                    int(proto.Cmd.DRIVE_STOP) | proto.RESPONSE_BIT,
                    int(proto.Cmd.SET_SPEED) | proto.RESPONSE_BIT,
                    int(proto.Cmd.SPEED_STOP) | proto.RESPONSE_BIT,
                    int(proto.Cmd.SET_ANGLE) | proto.RESPONSE_BIT,
                    int(proto.Cmd.ANGLE_STOP) | proto.RESPONSE_BIT,
                    int(proto.Cmd.SET_DISTANCE) | proto.RESPONSE_BIT,
                    int(proto.Cmd.DISTANCE_STOP) | proto.RESPONSE_BIT,
                    int(proto.Cmd.SET_LINE_FOLLOW) | proto.RESPONSE_BIT,
                    int(proto.Cmd.LINE_FOLLOW_STOP) | proto.RESPONSE_BIT,
                    int(proto.Cmd.CALIB_YAW) | proto.RESPONSE_BIT,
                ):
                    return
                self.log.emit(
                    f"未匹配的应答 cmd=0x{frame.cmd:04X} seq={frame.seq} "
                    f"len={len(frame.payload)}"
                )
            return

    def _set_link_alive(self, alive: bool) -> None:
        if self._link_alive != alive:
            self._link_alive = alive
            self.link_alive_changed.emit(alive)

    def _on_param_read(self, param_id: int, frame: proto.Frame) -> None:
        if frame is None:
            self.log.emit(f"PARAM_READ {param_id} 超时")
            return
        if frame.is_nak:
            code = frame.err_code or 0
            self.log.emit(f"PARAM_READ {param_id} 失败: {proto.err_text(code)}")
            return
        self.param_read_result.emit(param_id, frame.payload)

    def _on_param_write(self, param_id: int, frame: proto.Frame) -> None:
        if frame is None:
            self.log.emit(f"PARAM_WRITE {param_id} 超时")
            return
        if frame.is_nak:
            code = frame.err_code or 0
            msg = proto.err_text(code)
            self.param_write_result.emit(param_id, False, msg)
            self.log.emit(f"PARAM_WRITE {param_id} 失败: {msg}")
            return
        self.param_write_result.emit(param_id, True, "OK")

    def _on_param_list(self, frame: proto.Frame) -> None:
        if frame.is_nak:
            code = frame.err_code or 0
            self.log.emit(f"PARAM_LIST 失败: {proto.err_text(code)}")
            return
        entries = proto.parse_param_list(frame.payload)
        self.param_list_received.emit(entries)

    def _start_set_speed_attempt(self) -> None:
        payload = self._speed_payload
        if payload is None:
            return
        if self._speed_attempt == 0:
            self._begin_critical()
            fmt = payload[0] if payload else 0
            quiet_s = (
                proto.SET_SPEED_QUIET_S_FMT_LR
                if fmt == 1
                else proto.SET_SPEED_QUIET_S_FMT0
            )
            self._wait_rx_quiet(quiet_s=quiet_s, timeout_s=1.0)
        else:
            self._flush_serial_input()
            self._wait_rx_quiet(quiet_s=0.10, timeout_s=0.8)

        fmt = payload[0] if payload else 0
        attempt = self._speed_attempt
        timeout_s = 5.0 if fmt else 3.5

        def on_response(frame: Optional[proto.Frame]) -> None:
            if frame is not None and not frame.is_nak:
                self._speed_payload = None
                self._speed_attempt = 0
                self._finish_set_speed(frame)
                return
            if attempt < 2:
                self._speed_attempt = attempt + 1
                self._cmd_queue.put(("set_speed_retry", b"", None))
                return
            self._speed_payload = None
            self._speed_attempt = 0
            self._finish_set_speed(frame)

        self._post_request(
            int(proto.Cmd.SET_SPEED),
            payload,
            on_response=on_response,
            timeout=timeout_s,
            duplicate_tx=proto.HC05_SAFE_TX,
        )

    def _post_speed_stop(self) -> None:
        self._begin_critical()
        self._wait_rx_quiet(quiet_s=0.10, timeout_s=1.0)

        def on_response(frame: Optional[proto.Frame]) -> None:
            self._finish_speed_stop(frame)

        self._post_request(
            int(proto.Cmd.SPEED_STOP),
            b"",
            on_response=on_response,
            timeout=4.0,
            duplicate_tx=proto.HC05_SAFE_TX,
        )

    def _finish_set_speed(self, frame: Optional[proto.Frame]) -> None:
        self._on_set_speed_rsp(frame)
        self._end_critical()

    def _finish_speed_stop(self, frame: Optional[proto.Frame]) -> None:
        self._on_speed_stop_rsp(frame)
        self._end_critical()

    def _start_set_angle_attempt(self) -> None:
        payload = self._angle_payload
        if payload is None:
            return
        if self._angle_attempt == 0:
            self._begin_critical()
            self._wait_rx_quiet(quiet_s=proto.SET_SPEED_QUIET_S_FMT0, timeout_s=1.0)
        else:
            self._flush_serial_input()
            self._wait_rx_quiet(quiet_s=0.10, timeout_s=0.8)

        attempt = self._angle_attempt
        timeout_s = 3.5

        def on_response(frame: Optional[proto.Frame]) -> None:
            if frame is not None and not frame.is_nak:
                self._angle_payload = None
                self._angle_attempt = 0
                self._finish_set_angle(frame)
                return
            if attempt < 2:
                self._angle_attempt = attempt + 1
                self._cmd_queue.put(("set_angle_retry", b"", None))
                return
            self._angle_payload = None
            self._angle_attempt = 0
            self._finish_set_angle(frame)

        self._post_request(
            int(proto.Cmd.SET_ANGLE),
            payload,
            on_response=on_response,
            timeout=timeout_s,
            duplicate_tx=proto.HC05_SAFE_TX,
        )

    def _post_angle_stop(self) -> None:
        self._begin_critical()
        self._wait_rx_quiet(quiet_s=0.10, timeout_s=1.0)

        def on_response(frame: Optional[proto.Frame]) -> None:
            self._finish_angle_stop(frame)

        self._post_request(
            int(proto.Cmd.ANGLE_STOP),
            b"",
            on_response=on_response,
            timeout=4.0,
            duplicate_tx=proto.HC05_SAFE_TX,
        )

    def _apply_drive_stream(self, payload: bytes) -> None:
        throttle, steer = struct.unpack_from("<hh", payload, 0)
        if throttle == 0 and steer == 0:
            self._drive_stream_active = False
            self._drive_throttle = 0
            self._drive_steer = 0
            if self.is_connected():
                frame = self._send_request_retry(
                    int(proto.Cmd.DRIVE_STOP),
                    b"",
                    timeout=1.5,
                    retries=1,
                )
                self._on_drive_stop_rsp(frame)
            return

        was_active = self._drive_stream_active
        self._drive_throttle = throttle
        self._drive_steer = steer
        self._drive_stream_active = True
        if not was_active:
            # 首次激活立即发一帧，避免主循环最多 5 ms 的启动延迟
            self._drive_next_send = 0.0
            self._send_drive_stream_frame()
        # 已激活时仅更新目标值，由主循环按 DRIVE_STREAM_INTERVAL_S 节奏发送，
        # 避免鼠标事件高频触发（~60 Hz）导致 DRIVE 帧泛滥。

    def _send_drive_stream_frame(self) -> None:
        if not self.is_connected():
            return
        payload = proto.build_drive(self._drive_throttle, self._drive_steer)
        seq = self._next_seq()
        frame_bytes = proto.encode_frame(int(proto.Cmd.DRIVE), seq, payload)
        if not self._write_frame(frame_bytes):
            self.log.emit("DRIVE 流发送失败")

    def _nav_is_superseded(self, nav_id: int) -> bool:
        return self._nav_latest_id > nav_id

    def _coalesce_navigate_requests(self) -> None:
        """合并队列中积压的 navigate，只保留最新目标。"""
        deferred: list[tuple[str, bytes, Optional[Callable]]] = []
        while True:
            try:
                item = self._cmd_queue.get_nowait()
            except queue.Empty:
                break
            kind, payload, cb = item
            if kind == "navigate":
                continue
            deferred.append(item)
        for item in deferred:
            self._cmd_queue.put(item)

    def _process_navigate_chain(self) -> None:
        while self._nav_latest_payload is not None:
            nav_id = self._nav_latest_id
            payload = self._nav_latest_payload
            superseded, result = self._run_navigate_once(payload, nav_id)
            if superseded:
                self.navigation_finished.emit(result)
                self._coalesce_navigate_requests()
                continue
            self.navigation_finished.emit(result)
            break

    def _make_nav_result(
        self,
        *,
        nav_id: int,
        superseded: bool = False,
        command_failed: bool = False,
        delta_yaw: int = 0,
        dist_mm: int = 0,
        turned: bool = False,
        drove: bool = False,
        angle_done: bool = False,
        distance_done: bool = False,
        actual_delta_yaw: int = 0,
        final_dist_mm: Optional[int] = None,
    ) -> NavResult:
        return NavResult(
            ok=not superseded and not command_failed,
            command_failed=command_failed,
            superseded=superseded,
            nav_id=nav_id,
            delta_yaw=delta_yaw,
            dist_mm=dist_mm,
            turned=turned,
            drove=drove,
            angle_done=angle_done,
            distance_done=distance_done,
            actual_delta_yaw=actual_delta_yaw,
            final_dist_mm=final_dist_mm,
        )

    def _nav_abort_result(self, nav_id: int) -> NavResult:
        self.log.emit("地图导航: 目标已更新，停止当前趟")
        return self._make_nav_result(nav_id=nav_id, superseded=True)

    def _send_nav_request(
        self,
        cmd: int,
        payload: bytes = b"",
        *,
        timeout: float = 4.0,
        retries: int = 2,
    ) -> Optional[proto.Frame]:
        """导航专用：订阅推送繁忙时不等待线路空闲，直接发并延长超时。"""
        for attempt in range(retries):
            if attempt > 0:
                time.sleep(0.08)
                self._pump_serial()
            frame = self._send_request(
                cmd,
                payload,
                timeout=timeout,
                duplicate_tx=self._critical_cmd(cmd),
            )
            if frame is not None:
                return frame
        return None

    def _absorb_orphan_acks(self, duration_s: float = 0.35) -> None:
        deadline = time.monotonic() + duration_s
        while time.monotonic() < deadline:
            self._pump_serial()
            time.sleep(0.01)

    def _nav_send_maneuver(
        self,
        cmd: int,
        payload: bytes,
        *,
        wait_ack: bool = True,
    ) -> Optional[proto.Frame]:
        if wait_ack:
            return self._send_nav_request(cmd, payload)
        seq = self._next_seq()
        frame_bytes = proto.encode_frame(cmd, seq, payload)
        if not self._write_frame(frame_bytes):
            return None
        if self._critical_cmd(cmd) and proto.HC05_SAFE_TX:
            time.sleep(proto.SET_SPEED_DUP_TX_INTERVAL_S)
            self._write_frame(frame_bytes)
        return proto.Frame(cmd=cmd, seq=seq, flags=0, payload=b"")

    def _wait_angle_maneuver(
        self,
        expected_delta_yaw: int,
        nav_id: int,
        max_s: float = proto.NAV_MANEUVER_MAX_S,
    ) -> ManeuverWait:
        overall_deadline = time.monotonic() + max_s
        last_current: Optional[int] = None
        near_tol = proto.maneuver_angle_done_tol_deg(expected_delta_yaw)

        while time.monotonic() < overall_deadline:
            if self._nav_is_superseded(nav_id):
                return ManeuverWait(False, timed_out=True, value=last_current)

            sample = self._last_angle_loop
            if sample is not None and sample.target_yaw == expected_delta_yaw:
                last_current = sample.current_yaw
                err = abs(sample.target_yaw - sample.current_yaw)
                if err <= near_tol:
                    return ManeuverWait(True, value=last_current)

            self._pump_serial()
            self._coalesce_navigate_requests()
            time.sleep(0.05)

        return ManeuverWait(False, timed_out=True, value=last_current)

    def _wait_distance_maneuver(
        self,
        expected_dist_mm: int,
        nav_id: int,
        max_s: float = proto.NAV_MANEUVER_MAX_S,
    ) -> ManeuverWait:
        overall_deadline = time.monotonic() + max_s
        last_current: Optional[int] = None
        near_tol = proto.maneuver_distance_done_tol_mm(expected_dist_mm)

        while time.monotonic() < overall_deadline:
            if self._nav_is_superseded(nav_id):
                return ManeuverWait(False, timed_out=True, value=last_current)

            sample = self._last_distance_loop
            if sample is not None and sample.target_mm == expected_dist_mm:
                last_current = sample.current_mm
                err = abs(sample.target_mm - sample.current_mm)
                if err <= near_tol:
                    return ManeuverWait(True, value=last_current)

            self._pump_serial()
            self._coalesce_navigate_requests()
            time.sleep(0.05)

        return ManeuverWait(False, timed_out=True, value=last_current)

    def _stop_nav_legs(self, *, wait_ack: bool = True) -> None:
        if wait_ack:
            self._send_nav_request(
                int(proto.Cmd.DISTANCE_STOP), b"", timeout=2.0, retries=1
            )
            self._send_nav_request(
                int(proto.Cmd.ANGLE_STOP), b"", timeout=2.0, retries=1
            )
            return
        self._nav_send_maneuver(int(proto.Cmd.DISTANCE_STOP), b"", wait_ack=False)
        self._nav_send_maneuver(int(proto.Cmd.ANGLE_STOP), b"", wait_ack=False)
        self._absorb_orphan_acks()

    def _run_navigate_once(self, payload: bytes, nav_id: int) -> tuple[bool, NavResult]:
        delta_yaw, dist_mm, max_turn, max_drive = struct.unpack_from("<hiii", payload, 0)
        self._drive_stream_active = False
        self._begin_critical()
        self._pump_serial()
        self._send_nav_request(int(proto.Cmd.DRIVE_STOP), b"", timeout=2.0, retries=1)
        self._stop_nav_legs(wait_ack=True)
        self._pump_serial()

        if self._nav_is_superseded(nav_id):
            self._end_critical()
            return True, self._nav_abort_result(nav_id)

        self._last_angle_loop = None
        self._last_distance_loop = None

        command_failed = False
        turned = False
        drove = False
        angle_done = False
        distance_done = False
        actual_delta_yaw = 0
        final_dist_mm: Optional[int] = None

        if abs(delta_yaw) >= 4:
            turned = True
            self._last_angle_loop = None
            frame = self._nav_send_maneuver(
                int(proto.Cmd.SET_ANGLE),
                proto.build_set_angle(delta_yaw, 0, max_turn),
                wait_ack=True,
            )
            if self._nav_is_superseded(nav_id):
                self._stop_nav_legs(wait_ack=False)
                self._end_critical()
                return True, self._nav_abort_result(nav_id)
            if frame is None or frame.is_nak:
                self.log.emit("地图导航: 转角指令失败，跳过本步")
                command_failed = True
            else:
                wait = self._wait_angle_maneuver(delta_yaw, nav_id)
                if self._nav_is_superseded(nav_id):
                    self._stop_nav_legs(wait_ack=False)
                    self._end_critical()
                    return True, self._nav_abort_result(nav_id)
                if wait.value is not None:
                    actual_delta_yaw = wait.value
                elif not wait.done:
                    actual_delta_yaw = delta_yaw
                if wait.done:
                    angle_done = True
                    self.log.emit(
                        f"地图导航: 转角到位 Δθ={actual_delta_yaw:+d}° / 目标 {delta_yaw:+d}°"
                    )
                else:
                    self.log.emit(
                        f"地图导航: 转角超时（{proto.NAV_MANEUVER_MAX_S:.0f}s 内未接近目标"
                        f"，约 {actual_delta_yaw:+d}°），继续下一步"
                    )
            self._stop_nav_legs(wait_ack=True)
            self._pump_serial()

        if self._nav_is_superseded(nav_id):
            self._end_critical()
            return True, self._nav_abort_result(nav_id)

        if abs(dist_mm) >= 20:
            drove = True
            self._last_distance_loop = None
            self._pump_serial()
            frame = self._nav_send_maneuver(
                int(proto.Cmd.SET_DISTANCE),
                proto.build_set_distance(dist_mm, max_drive),
                wait_ack=True,
            )
            if self._nav_is_superseded(nav_id):
                self._stop_nav_legs(wait_ack=False)
                self._end_critical()
                return True, self._nav_abort_result(nav_id)
            if frame is None or frame.is_nak:
                self.log.emit("地图导航: 距离指令失败")
                command_failed = True
            else:
                wait = self._wait_distance_maneuver(dist_mm, nav_id)
                if self._nav_is_superseded(nav_id):
                    self._stop_nav_legs(wait_ack=False)
                    self._end_critical()
                    return True, self._nav_abort_result(nav_id)
                if wait.value is not None:
                    final_dist_mm = wait.value
                if wait.done:
                    distance_done = True
                    self.log.emit(
                        f"地图导航: 行驶到位 目标={dist_mm:+d} mm 实际≈"
                        f"{final_dist_mm if final_dist_mm is not None else 0:+d} mm"
                    )
                else:
                    self.log.emit(
                        f"地图导航: 行驶超时（{proto.NAV_MANEUVER_MAX_S:.0f}s 内未接近目标"
                        f"，约 {final_dist_mm if final_dist_mm is not None else 0:+d} mm），结束本趟"
                    )

        self._stop_nav_legs(wait_ack=True)
        self._last_angle_loop = None
        self._last_distance_loop = None
        self._pump_serial()
        self._end_critical()
        self.log.emit("地图导航: 本趟结束，可进行下一步")
        return False, self._make_nav_result(
            nav_id=nav_id,
            command_failed=command_failed,
            delta_yaw=delta_yaw,
            dist_mm=dist_mm,
            turned=turned,
            drove=drove,
            angle_done=angle_done,
            distance_done=distance_done,
            actual_delta_yaw=actual_delta_yaw,
            final_dist_mm=final_dist_mm,
        )

    def _run_calib_yaw(self, payload: bytes) -> None:
        """先停角度环/遥控，静止后再发 CALIB_YAW。"""
        self._begin_critical()
        self._wait_rx_quiet(quiet_s=0.08, timeout_s=0.6)
        self._send_request_retry(int(proto.Cmd.ANGLE_STOP), b"", timeout=2.0, retries=1)
        self._send_request_retry(int(proto.Cmd.DRIVE_STOP), b"", timeout=2.0, retries=1)
        self._wait_rx_quiet(quiet_s=0.20, timeout_s=1.0)

        def on_response(frame: Optional[proto.Frame]) -> None:
            self._finish_calib_yaw(frame)

        self._post_request(
            int(proto.Cmd.CALIB_YAW),
            payload,
            on_response=on_response,
            timeout=4.0,
            duplicate_tx=proto.HC05_SAFE_TX,
        )

    def _finish_set_angle(self, frame: Optional[proto.Frame]) -> None:
        self._on_set_angle_rsp(frame)
        self._end_critical()

    def _finish_angle_stop(self, frame: Optional[proto.Frame]) -> None:
        self._on_angle_stop_rsp(frame)
        self._end_critical()

    def _finish_calib_yaw(self, frame: Optional[proto.Frame]) -> None:
        self._on_calib_yaw_rsp(frame)
        self._end_critical()

    def _on_set_angle_rsp(self, frame: proto.Frame) -> None:
        if frame is None:
            self.log.emit("SET_ANGLE 超时（无应答）")
            return
        if frame.is_nak:
            code = frame.err_code or 0
            self.log.emit(f"SET_ANGLE 失败: {proto.err_text(code)}")
            return
        self.log.emit("SET_ANGLE 已确认")

    def _on_angle_stop_rsp(self, frame: proto.Frame) -> None:
        if frame is None:
            self.log.emit("ANGLE_STOP 超时")
            return
        if frame.is_nak:
            code = frame.err_code or 0
            self.log.emit(f"ANGLE_STOP 失败: {proto.err_text(code)}")
            return
        self.log.emit("ANGLE_STOP 已确认")

    def _start_set_distance_attempt(self) -> None:
        payload = self._distance_payload
        if payload is None:
            return
        if self._distance_attempt == 0:
            self._begin_critical()
            self._wait_rx_quiet(quiet_s=proto.SET_SPEED_QUIET_S_FMT0, timeout_s=1.0)
        else:
            self._flush_serial_input()
            self._wait_rx_quiet(quiet_s=0.10, timeout_s=0.8)

        attempt = self._distance_attempt
        timeout_s = 3.5

        def on_response(frame: Optional[proto.Frame]) -> None:
            if frame is not None and not frame.is_nak:
                self._distance_payload = None
                self._distance_attempt = 0
                self._finish_set_distance(frame)
                return
            if attempt < 2:
                self._distance_attempt = attempt + 1
                self._cmd_queue.put(("set_distance_retry", b"", None))
                return
            self._distance_payload = None
            self._distance_attempt = 0
            self._finish_set_distance(frame)

        self._post_request(
            int(proto.Cmd.SET_DISTANCE),
            payload,
            on_response=on_response,
            timeout=timeout_s,
            duplicate_tx=proto.HC05_SAFE_TX,
        )

    def _post_distance_stop(self) -> None:
        self._begin_critical()
        self._wait_rx_quiet(quiet_s=0.10, timeout_s=1.0)

        def on_response(frame: Optional[proto.Frame]) -> None:
            self._finish_distance_stop(frame)

        self._post_request(
            int(proto.Cmd.DISTANCE_STOP),
            b"",
            on_response=on_response,
            timeout=4.0,
            duplicate_tx=proto.HC05_SAFE_TX,
        )

    def _finish_set_distance(self, frame: Optional[proto.Frame]) -> None:
        self._on_set_distance_rsp(frame)
        self._end_critical()

    def _finish_distance_stop(self, frame: Optional[proto.Frame]) -> None:
        self._on_distance_stop_rsp(frame)
        self._end_critical()

    def _on_set_distance_rsp(self, frame: Optional[proto.Frame]) -> None:
        if frame is None:
            self.log.emit("SET_DISTANCE 超时（无应答）")
            return
        if frame.is_nak:
            code = frame.err_code or 0
            self.log.emit(f"SET_DISTANCE 失败: {proto.err_text(code)}")
            return
        self.log.emit("SET_DISTANCE 已确认")

    def _on_distance_stop_rsp(self, frame: Optional[proto.Frame]) -> None:
        if frame is None:
            self.log.emit("DISTANCE_STOP 超时")
            return
        if frame.is_nak:
            code = frame.err_code or 0
            self.log.emit(f"DISTANCE_STOP 失败: {proto.err_text(code)}")
            return
        self.log.emit("DISTANCE_STOP 已确认")

    def _on_calib_yaw_rsp(self, frame: Optional[proto.Frame]) -> None:
        if frame is None:
            self.log.emit("CALIB_YAW 超时（无应答）")
            return
        if frame.is_nak:
            code = frame.err_code or 0
            self.log.emit(f"CALIB_YAW 失败: {proto.err_text(code)}")
            return
        try:
            offset_deg, yaw_deg = proto.parse_calib_yaw_ack(frame.payload)
            self.log.emit(
                f"CALIB_YAW 成功 offset={offset_deg:+d}° yaw={yaw_deg:+d}°"
            )
        except ValueError as exc:
            self.log.emit(f"CALIB_YAW 应答解析失败: {exc}")

    def _on_set_speed_rsp(self, frame: proto.Frame) -> None:
        if frame is None:
            self.log.emit("SET_SPEED 超时（无应答）")
            return
        if frame.is_nak:
            code = frame.err_code or 0
            self.log.emit(f"SET_SPEED 失败: {proto.err_text(code)}")
            return
        self.log.emit("SET_SPEED 已确认")

    def _on_speed_stop_rsp(self, frame: proto.Frame) -> None:
        if frame is None:
            self.log.emit("SPEED_STOP 超时")
            return
        if frame.is_nak:
            code = frame.err_code or 0
            self.log.emit(f"SPEED_STOP 失败: {proto.err_text(code)}")
            return
        self.log.emit("SPEED_STOP 已确认")

    def _on_drive_rsp(self, frame: proto.Frame) -> None:
        if frame is None:
            self.log.emit("DRIVE 超时（无应答）")
            return
        if frame.is_nak:
            code = frame.err_code or 0
            self.log.emit(f"DRIVE 失败: {proto.err_text(code)}")
            return
        self.log.emit("DRIVE 已确认")

    def _on_drive_stop_rsp(self, frame: proto.Frame) -> None:
        if frame is None:
            self.log.emit("DRIVE_STOP 超时")
            return
        if frame.is_nak:
            code = frame.err_code or 0
            self.log.emit(f"DRIVE_STOP 失败: {proto.err_text(code)}")
            return
        self.log.emit("DRIVE_STOP 已确认")
