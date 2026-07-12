"""遥控 DRIVE 命令。"""

from __future__ import annotations

from typing import Optional

from PySide6.QtCore import Qt
from PySide6.QtWidgets import QHBoxLayout, QLabel, QPushButton, QSlider, QVBoxLayout, QWidget

from serial_worker import SerialWorker


class DriveTab(QWidget):
    def __init__(self, worker: SerialWorker, parent: Optional[QWidget] = None):
        super().__init__(parent)
        self._worker = worker

        layout = QVBoxLayout(self)
        self._throttle = QSlider(Qt.Orientation.Horizontal)
        self._throttle.setRange(-1000, 1000)
        self._steer = QSlider(Qt.Orientation.Horizontal)
        self._steer.setRange(-1000, 1000)
        layout.addWidget(QLabel("Throttle (-1000 ~ 1000)"))
        layout.addWidget(self._throttle)
        layout.addWidget(QLabel("Steer (-1000 ~ 1000)"))
        layout.addWidget(self._steer)

        row = QHBoxLayout()
        self._btn_send = QPushButton("发送 DRIVE")
        self._btn_stop = QPushButton("DRIVE_STOP")
        row.addWidget(self._btn_send)
        row.addWidget(self._btn_stop)
        layout.addLayout(row)
        layout.addStretch()

        self._btn_send.clicked.connect(self._send_drive)
        self._btn_stop.clicked.connect(self._worker.request_drive_stop)

    def _send_drive(self) -> None:
        self._worker.request_drive(self._throttle.value(), self._steer.value())
