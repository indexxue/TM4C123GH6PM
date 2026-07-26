"""相机 / 云台页：大预览区 + 关流 + 方向键舵机控制。"""

from __future__ import annotations

from typing import Optional

import tm_proto as proto
from PySide6.QtCore import Qt, QTimer, QUrl
from PySide6.QtGui import QDesktopServices, QImage, QPixmap
from PySide6.QtNetwork import QNetworkAccessManager, QNetworkReply, QNetworkRequest
from PySide6.QtWidgets import (
    QCheckBox,
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

        # --- 右侧：舵机 + 遥测 ---
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
        btn_center.clicked.connect(self._worker.request_cam_servo_center)
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
        btn_pan_abs.clicked.connect(
            lambda: self._worker.request_cam_servo_set_angle(0, self._deg.value())
        )
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

    def _nudge(self, ch: int, sign: int) -> None:
        delta = abs(int(self._delta.value())) * (1 if sign >= 0 else -1)
        self._worker.request_cam_servo_nudge(ch, delta)

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
        self._servo_label.setText(
            f"servo: valid={int(s.valid)}\n"
            f"pan={s.pan_deg_x100} tilt={s.tilt_deg_x100}\n"
            f"us={s.pan_pulse_us}/{s.tilt_pulse_us}"
        )

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
