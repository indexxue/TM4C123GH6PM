"""
串口收发线程：帧解析、SEQ 匹配、PING 保活、遥测轮询与订阅。
"""

from __future__ import annotations

import json
import queue
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


class SerialWorker(QThread):
    log = Signal(str)
    connected_changed = Signal(bool)
    hello_received = Signal(object)
    link_alive_changed = Signal(bool)
    telemetry_received = Signal(object)
    push_received = Signal(object)
    motor_rpm_received = Signal(object)
    encoder_counts_received = Signal(object)
    battery_received = Signal(int, int)
    optional_subscription_changed = Signal(int)
    param_read_result = Signal(int, bytes)
    param_write_result = Signal(int, bool, str)
    param_list_received = Signal(list)

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
        self._critical_depth = 0
        self._want_connected = False
        self._auto_reconnect = True
        self._baudrate = 115200
        self._reconnect_at = 0.0
        self._reconnect_backoff_s = 1.0

    @property
    def hello_info(self) -> Optional[proto.HelloInfo]:
        return self._hello_info

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

    @Slot(int, int)
    def request_drive(self, throttle: int, steer: int) -> None:
        self._cmd_queue.put(("drive", proto.build_drive(throttle, steer), None))

    @Slot()
    def request_drive_stop(self) -> None:
        self._cmd_queue.put(("drive_stop", b"", None))

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

    def _subscribe_plan(self, optional_mask: int) -> tuple[int, int, int, int, int, int]:
        """返回 (mask, hz_att, hz_enc, hz_line, hz_ultra, hz_motor_rpm)。"""
        mask = proto.BASE_CHANNEL_MASK | (optional_mask & proto.OPTIONAL_CHANNEL_MASK)
        hz_line = proto.DEFAULT_SUB_LINE_HZ if optional_mask & int(proto.TelChannel.LINE_ADC) else 0
        hz_ultra = proto.DEFAULT_SUB_ULTRA_HZ if optional_mask & int(proto.TelChannel.ULTRASONIC) else 0
        hz_motor = 0
        if optional_mask & int(proto.TelChannel.MOTOR_RPM):
            if self._hello_info and (self._hello_info.caps & int(proto.Cap.SPEED_LOOP)):
                hz_motor = proto.DEFAULT_SUB_MOTOR_RPM_HZ
        return (
            mask,
            proto.DEFAULT_SUB_ATT_HZ,
            proto.DEFAULT_SUB_ENC_HZ,
            hz_line,
            hz_ultra,
            hz_motor,
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
        mask, hz_att, hz_enc, hz_line, hz_ultra, hz_motor = self._subscribe_plan(self._optional_mask)
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
            elif kind == "drive":
                if self.is_connected():
                    frame = self._send_request_retry(
                        int(proto.Cmd.DRIVE),
                        payload,
                    )
                    self._on_drive_rsp(frame)
            elif kind == "drive_stop":
                if self.is_connected():
                    frame = self._send_request_retry(
                        int(proto.Cmd.DRIVE_STOP),
                        b"",
                    )
                    self._on_drive_stop_rsp(frame)

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
            self._apply_subscription_now(0)

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
        if req_cmd == int(proto.Cmd.SUBSCRIBE):
            return int(proto.Cmd.TELEMETRY_PUSH)
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
                elif push.channel_id == proto.CHANNEL_ID_ENCODER:
                    self.encoder_counts_received.emit(proto.parse_encoder_push(push.payload))
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
                    int(proto.Cmd.SET_SPEED) | proto.RESPONSE_BIT,
                    int(proto.Cmd.SPEED_STOP) | proto.RESPONSE_BIT,
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
