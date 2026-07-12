"""按需订阅：电量 / 循迹 / 超声波 / 电机 RPM（姿态+编码器由固件常驻推送）。"""

from __future__ import annotations

from typing import Optional

import tm_proto as proto
from PySide6.QtCore import Signal
from PySide6.QtWidgets import (
    QCheckBox,
    QGroupBox,
    QHBoxLayout,
    QLabel,
    QPushButton,
    QVBoxLayout,
    QWidget,
)


class SubscribePanel(QGroupBox):
    changed = Signal(int)

    def __init__(self, parent: Optional[QWidget] = None):
        super().__init__("按需订阅", parent)
        layout = QVBoxLayout(self)

        row = QHBoxLayout()
        self._chk_batt = QCheckBox("电量")
        self._chk_line = QCheckBox("循迹 0/1")
        self._chk_ultra = QCheckBox("超声波")
        self._chk_rpm = QCheckBox("电机 RPM")
        for chk in (self._chk_batt, self._chk_line, self._chk_ultra, self._chk_rpm):
            row.addWidget(chk)
        layout.addLayout(row)

        btn_row = QHBoxLayout()
        self._btn_apply = QPushButton("应用订阅")
        self._lbl_hint = QLabel("姿态+编码器连接后自动推送")
        self._lbl_hint.setStyleSheet("color: #666;")
        btn_row.addWidget(self._btn_apply)
        btn_row.addWidget(self._lbl_hint, stretch=1)
        layout.addLayout(btn_row)

        self._btn_apply.clicked.connect(self._emit_mask)

    def optional_mask(self) -> int:
        mask = 0
        if self._chk_batt.isChecked():
            mask |= int(proto.TelChannel.BATT)
        if self._chk_line.isChecked():
            mask |= int(proto.TelChannel.LINE_ADC)
        if self._chk_ultra.isChecked():
            mask |= int(proto.TelChannel.ULTRASONIC)
        if self._chk_rpm.isChecked():
            mask |= int(proto.TelChannel.MOTOR_RPM)
        return mask

    def set_rpm_available(self, available: bool) -> None:
        self._chk_rpm.setEnabled(available)
        if not available:
            self._chk_rpm.setChecked(False)

    def reset(self) -> None:
        for chk in (self._chk_batt, self._chk_line, self._chk_ultra, self._chk_rpm):
            chk.setChecked(False)

    def _emit_mask(self) -> None:
        self.changed.emit(self.optional_mask())
