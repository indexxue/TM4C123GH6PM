"""相机 / 云台页：大预览区 + 关流 + 方向键舵机控制 + Yaw 跟随。"""

from __future__ import annotations

import time
from typing import Optional

import tm_proto as proto
from PySide6.QtCore import Qt, QTimer, QUrl
from PySide6.QtGui import QDesktopServices, QImage, QPixmap
from PySide6.QtNetwork import QNetworkAccessManager, QNetworkReply, QNetworkRequest
from PySide6.QtWidgets import (
    QCheckBox,
    QComboBox,
    QGridLayout,
    QGroupBox,
    QHBoxLayout,
    QLabel,
    QPushButton,
    QSizePolicy,
    QSpinBox,
    QSplitter,
    QVBoxLayout,
    QWidget,
)

from serial_worker import SerialWorker

# Pan 中位：180.00° → 18000（deg×100）；镜头相对车头偏角 = pan/100 - 180
PAN_CENTER_X100 = 18000
# 无遥测限位时的软限位（与 SPI 约定一致）
PAN_SOFT_MIN_X100 = 0
PAN_SOFT_MAX_X100 = 36000
# 跟随下发：最小间隔与死区，避免刷爆蓝牙
FOLLOW_MIN_INTERVAL_S = 0.10
FOLLOW_DEADBAND_X100 = 100  # 1.00°


def _wrap_deg_180(deg: float) -> float:
    x = (float(deg) + 180.0) % 360.0 - 180.0
    if x <= -180.0:
        x += 360.0
    return x


def _pan_body_deg(pan_x100: int) -> float:
    """舵机 pan 相对车头偏角（°）：中位 180° → 0。"""
    return float(pan_x100) / 100.0 - 180.0


def _body_to_pan_x100(body_deg: float) -> int:
    return int(round((180.0 + float(body_deg)) * 100.0))


class CameraTab(QWidget):
    def __init__(self, worker: SerialWorker, parent: Optional[QWidget] = None) -> None:
        super().__init__(parent)
        self._worker = worker
        self._net: Optional[proto.CamNetInfo] = None
        self._preview_on = False
        self._last_pixmap: Optional[QPixmap] = None
        self._nam = QNetworkAccessManager(self)
        self._nam.finished.connect(self._on_http_finished)
        self._reply: Optional[QNetworkReply] = None

        self._car_yaw: Optional[float] = None
        self._pan_x100: Optional[int] = None
        self._pan_min_x100 = PAN_SOFT_MIN_X100
        self._pan_max_x100 = PAN_SOFT_MAX_X100
        self._follow_on = False
        self._world_lock_deg: Optional[float] = None
        self._last_cmd_pan_x100: Optional[int] = None
        self._last_cmd_ts = 0.0

        root = QHBoxLayout(self)
        splitter = QSplitter(Qt.Horizontal)
        root.addWidget(splitter)

        # --- 左侧：大图传预览 ---
        stream = QGroupBox("图传预览（WiFi HTTP）")
        stream_l = QVBoxLayout(stream)
        bar = QHBoxLayout()
        self._btn_net = QPushButton("获取 IP")
        self._btn_net.clicked.connect(self._worker.request_cam_net)
        self._btn_start = QPushButton("打开预览")
        self._btn_start.clicked.connect(self._start_preview)
        self._btn_stop = QPushButton("关闭预览")
        self._btn_stop.clicked.connect(self._stop_preview)
        self._btn_stop.setEnabled(False)
        self._btn_browser = QPushButton("浏览器 MJPEG")
        self._btn_browser.clicked.connect(self._open_stream)
        bar.addWidget(self._btn_net)
        bar.addWidget(self._btn_start)
        bar.addWidget(self._btn_stop)
        bar.addWidget(self._btn_browser)
        bar.addStretch(1)
        stream_l.addLayout(bar)

        self._url_label = QLabel("URL: —")
        self._url_label.setTextInteractionFlags(Qt.TextSelectableByMouse)
        self._url_label.setWordWrap(True)
        stream_l.addWidget(self._url_label)

        self._image = QLabel("无预览\n点击「打开预览」或「获取 IP」")
        self._image.setMinimumSize(480, 360)
        self._image.setAlignment(Qt.AlignCenter)
        self._image.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Expanding)
        self._image.setStyleSheet("background:#1a1a1a; color:#888; border:1px solid #444;")
        stream_l.addWidget(self._image, stretch=1)
        splitter.addWidget(stream)

        # --- 右侧：舵机 + 跟随 + 遥测 ---
        right = QWidget()
        right_l = QVBoxLayout(right)
        right_l.setContentsMargins(0, 0, 0, 0)

        pad = QGroupBox("舵机快捷键（nudge）")
        pad_l = QVBoxLayout(pad)

        step_row = QHBoxLayout()
        step_row.addWidget(QLabel("步进×100"))
        self._delta = QSpinBox()
        self._delta.setRange(100, 9000)
        self._delta.setValue(500)
        self._delta.setSingleStep(100)
        self._delta.setToolTip("每次按键相对微调量（deg×100），默认 5°")
        step_row.addWidget(self._delta)
        step_row.addStretch(1)
        pad_l.addLayout(step_row)

        grid = QGridLayout()
        btn_up = QPushButton("↑ Tilt+")
        btn_down = QPushButton("↓ Tilt−")
        btn_left = QPushButton("← Pan−")
        btn_right = QPushButton("→ Pan+")
        btn_center = QPushButton("回中")
        for b in (btn_up, btn_down, btn_left, btn_right, btn_center):
            b.setMinimumHeight(44)
            b.setMinimumWidth(88)
        btn_up.clicked.connect(lambda: self._nudge(1, +1))
        btn_down.clicked.connect(lambda: self._nudge(1, -1))
        btn_left.clicked.connect(lambda: self._nudge(0, -1))
        btn_right.clicked.connect(lambda: self._nudge(0, +1))
        btn_center.clicked.connect(self._on_center)
        grid.addWidget(btn_up, 0, 1)
        grid.addWidget(btn_left, 1, 0)
        grid.addWidget(btn_center, 1, 1)
        grid.addWidget(btn_right, 1, 2)
        grid.addWidget(btn_down, 2, 1)
        pad_l.addLayout(grid)

        abs_row = QHBoxLayout()
        self._deg = QSpinBox()
        self._deg.setRange(0, 36000)
        self._deg.setValue(18000)
        self._deg.setSingleStep(500)
        self._deg.setToolTip("绝对角 deg×100（18000=180°）")
        btn_pan_abs = QPushButton("设 Pan")
        btn_pan_abs.clicked.connect(self._on_set_pan_abs)
        btn_tilt_abs = QPushButton("设 Tilt")
        btn_tilt_abs.clicked.connect(
            lambda: self._worker.request_cam_servo_set_angle(1, self._deg.value())
        )
        abs_row.addWidget(QLabel("绝对角×100"))
        abs_row.addWidget(self._deg)
        abs_row.addWidget(btn_pan_abs)
        abs_row.addWidget(btn_tilt_abs)
        pad_l.addLayout(abs_row)

        det_row = QHBoxLayout()
        btn_det_on = QPushButton("检测开")
        btn_det_on.clicked.connect(lambda: self._worker.request_cam_detect_enable(True))
        btn_det_off = QPushButton("检测关")
        btn_det_off.clicked.connect(lambda: self._worker.request_cam_detect_enable(False))
        btn_snap = QPushButton("读快照")
        btn_snap.clicked.connect(self._worker.request_cam_snapshot)
        det_row.addWidget(btn_det_on)
        det_row.addWidget(btn_det_off)
        det_row.addWidget(btn_snap)
        pad_l.addLayout(det_row)
        right_l.addWidget(pad)

        follow = QGroupBox("Yaw 跟随")
        follow_l = QVBoxLayout(follow)
        self._chk_follow = QCheckBox("启用跟随")
        self._chk_follow.setToolTip(
            "固定偏角：镜头相对车头夹角恒为 offset\n"
            "世界锁定：开启时记下世界朝向，车转时反向补 pan"
        )
        self._chk_follow.toggled.connect(self._on_follow_toggled)
        follow_l.addWidget(self._chk_follow)

        mode_row = QHBoxLayout()
        mode_row.addWidget(QLabel("模式"))
        self._follow_mode = QComboBox()
        self._follow_mode.addItem("固定偏角（相对车头）", "offset")
        self._follow_mode.addItem("世界锁定（补偿车转）", "world")
        self._follow_mode.currentIndexChanged.connect(self._on_follow_mode_changed)
        mode_row.addWidget(self._follow_mode, stretch=1)
        follow_l.addLayout(mode_row)

        off_row = QHBoxLayout()
        off_row.addWidget(QLabel("偏角 offset"))
        self._offset_spin = QSpinBox()
        self._offset_spin.setRange(-180, 180)
        self._offset_spin.setSingleStep(5)
        self._offset_spin.setValue(0)
        self._offset_spin.setSuffix(" °")
        self._offset_spin.setToolTip(
            "镜头相对车头的偏角：+右 / −左。固定偏角模式下 pan = 180° + offset"
        )
        self._offset_spin.valueChanged.connect(self._on_offset_changed)
        btn_capture = QPushButton("捕获当前")
        btn_capture.setToolTip("用当前 pan 相对中位写入 offset")
        btn_capture.clicked.connect(self._capture_offset)
        off_row.addWidget(self._offset_spin, stretch=1)
        off_row.addWidget(btn_capture)
        follow_l.addLayout(off_row)

        self._follow_status = QLabel("跟随: 关")
        self._follow_status.setWordWrap(True)
        follow_l.addWidget(self._follow_status)
        right_l.addWidget(follow)

        tel = QGroupBox("遥测")
        tel_l = QVBoxLayout(tel)
        self._sub_detect = QCheckBox("订阅检测")
        self._sub_servo = QCheckBox("订阅云台")
        self._sub_detect.stateChanged.connect(self._on_sub_changed)
        self._sub_servo.stateChanged.connect(self._on_sub_changed)
        tel_l.addWidget(self._sub_detect)
        tel_l.addWidget(self._sub_servo)
        self._detect_label = QLabel("detect: —")
        self._servo_label = QLabel("servo: —")
        self._detect_label.setWordWrap(True)
        self._servo_label.setWordWrap(True)
        tel_l.addWidget(self._detect_label)
        tel_l.addWidget(self._servo_label)
        right_l.addWidget(tel)
        right_l.addStretch(1)

        splitter.addWidget(right)
        splitter.setStretchFactor(0, 4)
        splitter.setStretchFactor(1, 1)
        splitter.setSizes([720, 280])

        self._preview_timer = QTimer(self)
        self._preview_timer.setInterval(200)
        self._preview_timer.timeout.connect(self._poll_jpeg)

        self._worker.cam_detect_received.connect(self._on_detect)
        self._worker.cam_servo_received.connect(self._on_servo)
        self._worker.cam_net_received.connect(self._on_net)
        self._worker.optional_subscription_changed.connect(self._sync_sub_checks)
        self._optional_mask = 0

    def resizeEvent(self, event) -> None:  # noqa: N802
        super().resizeEvent(event)
        if self._last_pixmap is not None and not self._last_pixmap.isNull():
            self._image.setPixmap(
                self._last_pixmap.scaled(
                    self._image.size(), Qt.KeepAspectRatio, Qt.SmoothTransformation
                )
            )

    def reset(self) -> None:
        self._chk_follow.blockSignals(True)
        self._chk_follow.setChecked(False)
        self._chk_follow.blockSignals(False)
        self._follow_on = False
        self._world_lock_deg = None
        self._car_yaw = None
        self._pan_x100 = None
        self._last_cmd_pan_x100 = None
        self._follow_status.setText("跟随: 关")

    def on_attitude_yaw(self, yaw: float) -> None:
        """主窗口姿态推送入口（度）。"""
        self._car_yaw = _wrap_deg_180(yaw)
        self._update_follow_status()
        if self._follow_on:
            self._follow_tick(force=False)

    def _nudge(self, ch: int, sign: int) -> None:
        delta = abs(int(self._delta.value())) * (1 if sign >= 0 else -1)
        if ch == 0 and self._follow_on:
            # 手动 nudge pan：改写 offset，保持跟随语义
            new_off = int(round(self._offset_spin.value() + delta / 100.0))
            new_off = max(-180, min(180, new_off))
            self._offset_spin.blockSignals(True)
            self._offset_spin.setValue(new_off)
            self._offset_spin.blockSignals(False)
            if self._follow_mode.currentData() == "world" and self._car_yaw is not None:
                self._world_lock_deg = _wrap_deg_180(self._car_yaw + float(new_off))
            self._follow_tick(force=True)
            return
        self._worker.request_cam_servo_nudge(ch, delta)

    def _on_center(self) -> None:
        if self._follow_on:
            self._offset_spin.blockSignals(True)
            self._offset_spin.setValue(0)
            self._offset_spin.blockSignals(False)
            if self._follow_mode.currentData() == "world" and self._car_yaw is not None:
                self._world_lock_deg = self._car_yaw
            self._follow_tick(force=True)
            return
        self._worker.request_cam_servo_center()

    def _on_set_pan_abs(self) -> None:
        deg = int(self._deg.value())
        if self._follow_on:
            off = int(round(_pan_body_deg(deg)))
            off = max(-180, min(180, off))
            self._offset_spin.blockSignals(True)
            self._offset_spin.setValue(off)
            self._offset_spin.blockSignals(False)
            if self._follow_mode.currentData() == "world" and self._car_yaw is not None:
                self._world_lock_deg = _wrap_deg_180(self._car_yaw + float(off))
            self._follow_tick(force=True)
            return
        self._worker.request_cam_servo_set_angle(0, deg)

    def _capture_offset(self) -> None:
        if self._pan_x100 is None:
            self._follow_status.setText("跟随: 无 pan 遥测，请先订阅云台或读快照")
            return
        off = int(round(_pan_body_deg(self._pan_x100)))
        off = max(-180, min(180, off))
        self._offset_spin.setValue(off)
        if self._follow_on and self._follow_mode.currentData() == "world":
            if self._car_yaw is not None:
                self._world_lock_deg = _wrap_deg_180(self._car_yaw + float(off))
            self._follow_tick(force=True)

    def _on_offset_changed(self, _value: int) -> None:
        if not self._follow_on:
            self._update_follow_status()
            return
        if self._follow_mode.currentData() == "world" and self._car_yaw is not None:
            self._world_lock_deg = _wrap_deg_180(
                self._car_yaw + float(self._offset_spin.value())
            )
        self._follow_tick(force=True)

    def _on_follow_mode_changed(self) -> None:
        if not self._follow_on:
            self._update_follow_status()
            return
        self._arm_world_lock_if_needed()
        self._follow_tick(force=True)

    def _on_follow_toggled(self, checked: bool) -> None:
        self._follow_on = bool(checked)
        self._last_cmd_pan_x100 = None
        if self._follow_on:
            self._ensure_servo_subscribed()
            self._arm_world_lock_if_needed()
            self._follow_tick(force=True)
        else:
            self._world_lock_deg = None
            self._update_follow_status()

    def _arm_world_lock_if_needed(self) -> None:
        if self._follow_mode.currentData() != "world":
            self._world_lock_deg = None
            return
        if self._car_yaw is None:
            self._world_lock_deg = None
            return
        self._world_lock_deg = _wrap_deg_180(
            self._car_yaw + float(self._offset_spin.value())
        )

    def _ensure_servo_subscribed(self) -> None:
        mask = self._optional_mask | int(proto.TelChannel.CAM_SERVO)
        if mask == self._optional_mask:
            return
        if not self._sub_servo.isChecked():
            self._sub_servo.blockSignals(True)
            self._sub_servo.setChecked(True)
            self._sub_servo.blockSignals(False)
        self._worker.request_apply_subscription(mask)

    def _clamp_pan_x100(self, pan_x100: int) -> int:
        return max(self._pan_min_x100, min(self._pan_max_x100, int(pan_x100)))

    def _target_pan_x100(self) -> Optional[int]:
        offset = float(self._offset_spin.value())
        mode = self._follow_mode.currentData()
        if mode == "world":
            if self._car_yaw is None:
                return None
            if self._world_lock_deg is None:
                self._arm_world_lock_if_needed()
            if self._world_lock_deg is None:
                return None
            # 镜头世界朝向锁定；body = lock - car_yaw
            body = _wrap_deg_180(self._world_lock_deg - self._car_yaw)
            return self._clamp_pan_x100(_body_to_pan_x100(body))
        # 固定偏角：camera_world - car_yaw = offset ⇒ pan_body = offset
        return self._clamp_pan_x100(_body_to_pan_x100(offset))

    def _follow_tick(self, force: bool) -> None:
        if not self._follow_on:
            return
        target = self._target_pan_x100()
        if target is None:
            self._update_follow_status()
            return
        now = time.monotonic()
        if not force:
            if (
                self._last_cmd_pan_x100 is not None
                and abs(target - self._last_cmd_pan_x100) < FOLLOW_DEADBAND_X100
            ):
                self._update_follow_status()
                return
            if (now - self._last_cmd_ts) < FOLLOW_MIN_INTERVAL_S:
                self._update_follow_status()
                return
        self._last_cmd_pan_x100 = target
        self._last_cmd_ts = now
        self._worker.request_cam_servo_set_angle(0, target)
        self._update_follow_status()

    def _update_follow_status(self) -> None:
        yaw_s = "—" if self._car_yaw is None else f"{self._car_yaw:.0f}°"
        pan_s = "—" if self._pan_x100 is None else f"{self._pan_x100}"
        body = None if self._pan_x100 is None else _pan_body_deg(self._pan_x100)
        body_s = "—" if body is None else f"{body:+.0f}°"
        cam_world = None
        if self._car_yaw is not None and body is not None:
            cam_world = _wrap_deg_180(self._car_yaw + body)
        cam_s = "—" if cam_world is None else f"{cam_world:.0f}°"
        offset = int(self._offset_spin.value())
        if not self._follow_on:
            self._follow_status.setText(
                f"跟随: 关  |  车yaw={yaw_s}  pan={pan_s}  偏角={body_s}  镜头世界≈{cam_s}"
            )
            return
        mode = self._follow_mode.currentData()
        if mode == "world":
            lock_s = "—" if self._world_lock_deg is None else f"{self._world_lock_deg:.0f}°"
            tgt = self._target_pan_x100()
            tgt_s = "—" if tgt is None else str(tgt)
            self._follow_status.setText(
                f"跟随: 世界锁定 lock={lock_s}  目标pan={tgt_s}\n"
                f"车yaw={yaw_s}  pan={pan_s}  偏角={body_s}  镜头世界≈{cam_s}"
            )
        else:
            tgt = self._target_pan_x100()
            tgt_s = "—" if tgt is None else str(tgt)
            self._follow_status.setText(
                f"跟随: 固定偏角 offset={offset:+d}°  目标pan={tgt_s}\n"
                f"车yaw={yaw_s}  pan={pan_s}  偏角={body_s}  镜头世界≈{cam_s}"
            )

    def _sync_sub_checks(self, mask: int) -> None:
        self._optional_mask = mask
        self._sub_detect.blockSignals(True)
        self._sub_servo.blockSignals(True)
        self._sub_detect.setChecked(bool(mask & int(proto.TelChannel.CAM_DETECT)))
        self._sub_servo.setChecked(bool(mask & int(proto.TelChannel.CAM_SERVO)))
        self._sub_detect.blockSignals(False)
        self._sub_servo.blockSignals(False)

    def _on_sub_changed(self) -> None:
        mask = self._optional_mask & ~(
            int(proto.TelChannel.CAM_DETECT) | int(proto.TelChannel.CAM_SERVO)
        )
        if self._sub_detect.isChecked():
            mask |= int(proto.TelChannel.CAM_DETECT)
        if self._sub_servo.isChecked():
            mask |= int(proto.TelChannel.CAM_SERVO)
        self._worker.request_apply_subscription(mask)

    def _on_detect(self, det: object) -> None:
        d = det
        b0 = d.boxes[0] if d.boxes else proto.CamBox()
        self._detect_label.setText(
            f"detect: valid={int(d.valid)} n={d.count} best={d.best_index}\n"
            f"{d.frame_w}x{d.frame_h} box0=({b0.x},{b0.y},w={b0.w},sc={b0.score_u8})"
        )

    def _on_servo(self, servo: object) -> None:
        s = servo
        if s.valid:
            self._pan_x100 = int(s.pan_deg_x100)
            if s.pan_min_x100 != 0 or s.pan_max_x100 != 0:
                self._pan_min_x100 = int(s.pan_min_x100)
                self._pan_max_x100 = int(s.pan_max_x100)
        self._servo_label.setText(
            f"servo: valid={int(s.valid)}\n"
            f"pan={s.pan_deg_x100} tilt={s.tilt_deg_x100}\n"
            f"us={s.pan_pulse_us}/{s.tilt_pulse_us}"
        )
        self._update_follow_status()

    def _on_net(self, net: object) -> None:
        self._net = net
        self._url_label.setText(f"URL: {net.stream_url()}")
        if self._preview_on:
            self._poll_jpeg()

    def _open_stream(self) -> None:
        if self._net is None or not self._net.valid:
            self._worker.request_cam_net()
            return
        QDesktopServices.openUrl(QUrl(self._net.stream_url()))

    def _start_preview(self) -> None:
        if self._net is None or not (self._net.flags & 1):
            self._worker.request_cam_net()
        self._preview_on = True
        self._btn_start.setEnabled(False)
        self._btn_stop.setEnabled(True)
        self._preview_timer.start()
        self._poll_jpeg()

    def _stop_preview(self) -> None:
        self._preview_on = False
        self._preview_timer.stop()
        if self._reply is not None:
            self._reply.abort()
            self._reply = None
        self._last_pixmap = None
        self._image.clear()
        self._image.setText("预览已关闭")
        self._btn_start.setEnabled(True)
        self._btn_stop.setEnabled(False)

    def _poll_jpeg(self) -> None:
        if not self._preview_on or self._net is None:
            return
        if self._reply is not None:
            return
        req = QNetworkRequest(QUrl(self._net.snapshot_url()))
        self._reply = self._nam.get(req)

    def _on_http_finished(self, reply: QNetworkReply) -> None:
        if reply is self._reply:
            self._reply = None
        if not self._preview_on:
            reply.deleteLater()
            return
        if reply.error() != QNetworkReply.NetworkError.NoError:
            self._image.setText(f"预览失败: {reply.errorString()}")
            reply.deleteLater()
            return
        data = reply.readAll().data()
        reply.deleteLater()
        img = QImage.fromData(data, "JPG")
        if img.isNull():
            self._image.setText("无效 JPEG")
            return
        self._last_pixmap = QPixmap.fromImage(img)
        self._image.setPixmap(
            self._last_pixmap.scaled(
                self._image.size(), Qt.KeepAspectRatio, Qt.SmoothTransformation
            )
        )
