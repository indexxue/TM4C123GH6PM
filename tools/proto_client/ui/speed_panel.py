"""速度环控制面板（SET_SPEED / PID 快捷读）。"""

from __future__ import annotations

from typing import Callable, Optional

import tm_proto as proto
from PySide6.QtCore import Qt
from PySide6.QtWidgets import (
    QComboBox,
    QFormLayout,
    QGroupBox,
    QHBoxLayout,
    QLabel,
    QPushButton,
    QSpinBox,
    QVBoxLayout,
    QWidget,
)

from serial_worker import SerialWorker
from ui.param_panel import ParamEditor


class SpeedControlPanel(QGroupBox):
    MOTOR_NAMES = ["M1", "M2", "M3", "M4"]
    DEFAULT_RPM = 100
    RPM_STEP = 50

    def __init__(
        self,
        worker: SerialWorker,
        pid_editor: Optional[ParamEditor],
        spd_limit_editor: Optional[ParamEditor],
        on_clear_plot: Optional[Callable[[], None]] = None,
        parent: Optional[QWidget] = None,
    ):
        super().__init__("速度环调试", parent)
        self._worker = worker
        self._pid_editor = pid_editor
        self._spd_limit_editor = spd_limit_editor
        self._on_clear_plot = on_clear_plot
        self._target = [0, 0, 0, 0]
        self._motor_count = proto.MOTOR_COUNT_DEFAULT

        root = QVBoxLayout(self)

        pid_row = QHBoxLayout()
        self._btn_read_pid = QPushButton("读取 pid_speed")
        self._btn_read_lim = QPushButton("读取 spd_limit")
        pid_row.addWidget(self._btn_read_pid)
        pid_row.addWidget(self._btn_read_lim)
        pid_row.addStretch()
        root.addLayout(pid_row)

        cmd_form = QFormLayout()
        self._mode = QComboBox()
        self._mode.addItem("单轮 (format=0)", 0)
        self._mode.addItem("左右 (format=1)", 1)
        self._mode.addItem("四轮 (format=2)", 2)
        cmd_form.addRow("模式", self._mode)

        self._lr_hint = QLabel()
        self._lr_hint.setWordWrap(True)
        self._lr_hint.setStyleSheet("color: palette(mid);")
        cmd_form.addRow(self._lr_hint)

        self._motor_id = QSpinBox()
        self._rpm_single = self._make_rpm_spin()
        self._motor_id_row = QLabel("电机 ID")
        self._rpm_single_row = QLabel("RPM")
        cmd_form.addRow(self._motor_id_row, self._motor_id)
        cmd_form.addRow(self._rpm_single_row, self._rpm_single)

        self._rpm_left = self._make_rpm_spin()
        self._rpm_right = self._make_rpm_spin()
        self._rpm_left_row = QLabel("左组 RPM")
        self._rpm_right_row = QLabel("右组 RPM")
        cmd_form.addRow(self._rpm_left_row, self._rpm_left)
        cmd_form.addRow(self._rpm_right_row, self._rpm_right)

        self._rpm_m = [self._make_rpm_spin() for _ in range(4)]
        self._rpm_m_rows: list[QLabel] = []
        for i, sp in enumerate(self._rpm_m):
            row_lbl = QLabel(f"{self.MOTOR_NAMES[i]} RPM")
            self._rpm_m_rows.append(row_lbl)
            cmd_form.addRow(row_lbl, sp)

        btn_row = QHBoxLayout()
        self._btn_apply = QPushButton("下发 SET_SPEED")
        self._btn_stop = QPushButton("SPEED_STOP")
        self._btn_step = QPushButton("阶跃 M1=100")
        self._btn_clear = QPushButton("清空转速曲线")
        btn_row.addWidget(self._btn_apply)
        btn_row.addWidget(self._btn_stop)
        btn_row.addWidget(self._btn_step)
        btn_row.addWidget(self._btn_clear)
        cmd_form.addRow(btn_row)
        root.addLayout(cmd_form)

        self._status = QLabel("")
        root.addWidget(self._status)

        self._btn_read_pid.clicked.connect(self._read_pid)
        self._btn_read_lim.clicked.connect(self._read_limit)
        self._btn_apply.clicked.connect(self._apply_speed)
        self._btn_stop.clicked.connect(self._stop_speed)
        self._btn_step.clicked.connect(self._step_m1)
        self._btn_clear.clicked.connect(self._clear_plot)
        self._mode.currentIndexChanged.connect(self._update_mode_visibility)

        self._update_mode_visibility()
        self.set_motor_count(self._motor_count)

    @staticmethod
    def _make_rpm_spin() -> QSpinBox:
        sp = QSpinBox()
        sp.setRange(-500, 500)
        sp.setSingleStep(SpeedControlPanel.RPM_STEP)
        sp.setValue(SpeedControlPanel.DEFAULT_RPM)
        return sp

    def set_motor_count(self, count: int) -> None:
        count = max(2, min(proto.MOTOR_COUNT_MAX, int(count)))
        self._motor_count = count
        self._motor_id.setRange(1, count)
        if self._motor_id.value() > count:
            self._motor_id.setValue(1)
        for i in range(proto.MOTOR_COUNT_MAX):
            show = i < count
            self._rpm_m_rows[i].setVisible(show)
            self._rpm_m[i].setVisible(show)
        if count == 2:
            self._lr_hint.setText("左右模式：左=M1，右=M2（format=1 走 chassis LR）")
            self._rpm_left_row.setText("左 RPM (M1)")
            self._rpm_right_row.setText("右 RPM (M2)")
            self._mode.setItemText(1, "左右 (format=1)")
            self._mode.setItemText(2, "双轮 (format=2)")
        else:
            self._lr_hint.setText("左右模式：左=M1+M3，右=M2+M4")
            self._rpm_left_row.setText("左组 RPM (M1+M3)")
            self._rpm_right_row.setText("右组 RPM (M2+M4)")
            self._mode.setItemText(1, "左右 (format=1)")
            self._mode.setItemText(2, "四轮 (format=2)")
        self._update_mode_visibility()

    @property
    def target_rpms(self) -> list[int]:
        return list(self._target)

    def set_target_line_callback(self, callback) -> None:
        self._target_line_callback = callback

    def _notify_target_lines(self) -> None:
        cb = getattr(self, "_target_line_callback", None)
        if cb is not None:
            cb(self._target, self._mode.currentData(), self._motor_id.value())

    def _read_pid(self) -> None:
        if self._pid_editor is not None:
            self._pid_editor.read()
            self._set_status("已请求读取 pid_speed")

    def _read_limit(self) -> None:
        if self._spd_limit_editor is not None:
            self._spd_limit_editor.read()
            self._set_status("已请求读取 spd_limit")

    def _apply_speed(self) -> None:
        mode = self._mode.currentData()
        if mode == 0:
            mid = self._motor_id.value()
            rpm = self._rpm_single.value()
            self._target = [0, 0, 0, 0]
            self._target[mid - 1] = rpm
            payload = proto.build_set_speed_wheel(mid, rpm)
        elif mode == 1:
            left = self._rpm_left.value()
            right = self._rpm_right.value()
            self._target = [left, right, left, right]
            payload = proto.build_set_speed_lr(left, right)
        else:
            rpms = [sp.value() for sp in self._rpm_m[: self._motor_count]]
            if self._motor_count == 2:
                rpms.extend([0, 0])
            self._target = rpms
            payload = proto.build_set_speed_four(*rpms)
        self._worker.request_set_speed(payload)
        self._notify_target_lines()
        self._set_status("SET_SPEED 已发送")

    def _stop_speed(self) -> None:
        self._worker.request_speed_stop()
        self._target = [0, 0, 0, 0]
        self._notify_target_lines()
        self._set_status("SPEED_STOP 已发送")

    def _step_m1(self) -> None:
        self._mode.setCurrentIndex(0)
        self._motor_id.setValue(1)
        self._rpm_single.setValue(self.DEFAULT_RPM)
        self._apply_speed()

    def _clear_plot(self) -> None:
        if self._on_clear_plot is not None:
            self._on_clear_plot()

    def _set_status(self, text: str) -> None:
        self._status.setText(text)

    def _update_mode_visibility(self) -> None:
        mode = self._mode.currentData()
        self._motor_id_row.setVisible(mode == 0)
        self._motor_id.setVisible(mode == 0)
        self._motor_id.setEnabled(mode == 0)
        self._rpm_single_row.setVisible(mode == 0)
        self._rpm_single.setVisible(mode == 0)
        self._rpm_single.setEnabled(mode == 0)
        self._rpm_left_row.setVisible(mode == 1)
        self._rpm_left.setVisible(mode == 1)
        self._rpm_left.setEnabled(mode == 1)
        self._rpm_right_row.setVisible(mode == 1)
        self._rpm_right.setVisible(mode == 1)
        self._rpm_right.setEnabled(mode == 1)
        self._lr_hint.setVisible(mode == 1)
        for i, sp in enumerate(self._rpm_m):
            show = mode == 2 and i < self._motor_count
            self._rpm_m_rows[i].setVisible(show)
            sp.setVisible(show)
            sp.setEnabled(show)
        self._notify_target_lines()
