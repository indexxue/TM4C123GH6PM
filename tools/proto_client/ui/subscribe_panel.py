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
        self._chk_angle = QCheckBox("角度环")
        self._chk_distance = QCheckBox("距离环")
        for chk in (self._chk_batt, self._chk_line, self._chk_ultra, self._chk_rpm, self._chk_angle, self._chk_distance):
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
        if self._chk_angle.isChecked():
            mask |= int(proto.TelChannel.ANGLE_LOOP)
        if self._chk_distance.isChecked():
            mask |= int(proto.TelChannel.DISTANCE_LOOP)
        return mask

    def set_rpm_available(self, available: bool) -> None:
        self._chk_rpm.setEnabled(available)
        if not available:
            self._chk_rpm.setChecked(False)

    def set_angle_available(self, available: bool) -> None:
        self._chk_angle.setEnabled(available)
        if not available:
            self._chk_angle.setChecked(False)

    def set_distance_available(self, available: bool) -> None:
        self._chk_distance.setEnabled(available)
        if not available:
            self._chk_distance.setChecked(False)

    def apply_mask(self, mask: int) -> None:
        """同步勾选状态（连接后自动订阅时调用，不触发 changed）。"""
        self._chk_batt.blockSignals(True)
        self._chk_line.blockSignals(True)
        self._chk_ultra.blockSignals(True)
        self._chk_rpm.blockSignals(True)
        self._chk_angle.blockSignals(True)
        self._chk_distance.blockSignals(True)
        self._chk_batt.setChecked(bool(mask & int(proto.TelChannel.BATT)))
        self._chk_line.setChecked(bool(mask & int(proto.TelChannel.LINE_ADC)))
        self._chk_ultra.setChecked(bool(mask & int(proto.TelChannel.ULTRASONIC)))
        self._chk_rpm.setChecked(bool(mask & int(proto.TelChannel.MOTOR_RPM)))
        self._chk_angle.setChecked(bool(mask & int(proto.TelChannel.ANGLE_LOOP)))
        self._chk_distance.setChecked(bool(mask & int(proto.TelChannel.DISTANCE_LOOP)))
        self._chk_batt.blockSignals(False)
        self._chk_line.blockSignals(False)
        self._chk_ultra.blockSignals(False)
        self._chk_rpm.blockSignals(False)
        self._chk_angle.blockSignals(False)
        self._chk_distance.blockSignals(False)

    def reset(self) -> None:
        for chk in (self._chk_batt, self._chk_line, self._chk_ultra, self._chk_rpm, self._chk_angle, self._chk_distance):
            chk.setChecked(False)

    def _emit_mask(self) -> None:
        self.changed.emit(self.optional_mask())
