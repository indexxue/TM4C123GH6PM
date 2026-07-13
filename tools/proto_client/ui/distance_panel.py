"""距离环控制面板（SET_DISTANCE / pid_dist 快捷读）。"""

from __future__ import annotations

from typing import Callable, Optional

import tm_proto as proto
from PySide6.QtWidgets import (
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


class DistanceControlPanel(QGroupBox):
    DEFAULT_DIST_MM = 500
    DIST_STEP_MM = 100
    DEFAULT_MAX_RPM = 0

    def __init__(
        self,
        worker: SerialWorker,
        pid_dist_editor: Optional[ParamEditor],
        spd_limit_editor: Optional[ParamEditor],
        on_clear_plot: Optional[Callable[[], None]] = None,
        parent: Optional[QWidget] = None,
    ):
        super().__init__("距离环调试", parent)
        self._worker = worker
        self._pid_dist_editor = pid_dist_editor
        self._spd_limit_editor = spd_limit_editor
        self._on_clear_plot = on_clear_plot

        root = QVBoxLayout(self)

        pid_row = QHBoxLayout()
        self._btn_read_pid = QPushButton("读取 pid_dist")
        self._btn_read_lim = QPushButton("读取 spd_limit")
        pid_row.addWidget(self._btn_read_pid)
        pid_row.addWidget(self._btn_read_lim)
        pid_row.addStretch()
        root.addLayout(pid_row)

        cmd_form = QFormLayout()

        dist_row = QWidget()
        dist_layout = QHBoxLayout(dist_row)
        dist_layout.setContentsMargins(0, 0, 0, 0)
        self._btn_dist_dec = QPushButton(f"−{self.DIST_STEP_MM}")
        self._btn_dist_inc = QPushButton(f"+{self.DIST_STEP_MM}")
        self._dist_spin = QSpinBox()
        self._dist_spin.setRange(-10000, 10000)
        self._dist_spin.setSingleStep(self.DIST_STEP_MM)
        self._dist_spin.setValue(self.DEFAULT_DIST_MM)
        self._dist_spin.setSuffix(" mm")
        dist_layout.addWidget(self._btn_dist_dec)
        dist_layout.addWidget(self._dist_spin, stretch=1)
        dist_layout.addWidget(self._btn_dist_inc)
        cmd_form.addRow("相对位移 Δs", dist_row)

        self._max_rpm = QSpinBox()
        self._max_rpm.setRange(0, 500)
        self._max_rpm.setSingleStep(10)
        self._max_rpm.setValue(self.DEFAULT_MAX_RPM)
        self._max_rpm.setSpecialValueText("默认 (max_rpm)")
        cmd_form.addRow("速度上限 RPM", self._max_rpm)

        hint = QLabel(
            "SET_DISTANCE 为相对位移：从下发时刻起累计 Δs（mm）。"
            "正=前进，负=后退；到位后自动停车。"
        )
        hint.setWordWrap(True)
        hint.setStyleSheet("color: palette(mid);")
        cmd_form.addRow(hint)

        btn_row = QHBoxLayout()
        self._btn_apply = QPushButton("下发 SET_DISTANCE")
        self._btn_stop = QPushButton("DISTANCE_STOP")
        self._btn_step = QPushButton(f"阶跃 +{self.DEFAULT_DIST_MM} mm")
        self._btn_clear = QPushButton("清空距离曲线")
        btn_row.addWidget(self._btn_apply)
        btn_row.addWidget(self._btn_stop)
        btn_row.addWidget(self._btn_step)
        btn_row.addWidget(self._btn_clear)
        cmd_form.addRow(btn_row)

        self._lbl_status = QLabel("就绪")
        self._lbl_status.setWordWrap(True)
        cmd_form.addRow(self._lbl_status)

        root.addLayout(cmd_form)

        self._btn_read_pid.clicked.connect(self._read_pid)
        self._btn_read_lim.clicked.connect(self._read_lim)
        self._btn_dist_dec.clicked.connect(
            lambda: self._dist_spin.setValue(self._dist_spin.value() - self.DIST_STEP_MM)
        )
        self._btn_dist_inc.clicked.connect(
            lambda: self._dist_spin.setValue(self._dist_spin.value() + self.DIST_STEP_MM)
        )
        self._btn_apply.clicked.connect(self._apply)
        self._btn_stop.clicked.connect(self._stop)
        self._btn_step.clicked.connect(self._step)
        self._btn_clear.clicked.connect(self._clear_plot)

    def _set_status(self, text: str) -> None:
        self._lbl_status.setText(text)

    def _read_pid(self) -> None:
        if self._pid_dist_editor is not None:
            self._pid_dist_editor.read()
            self._set_status("已请求读取 pid_dist")

    def _read_lim(self) -> None:
        if self._spd_limit_editor is not None:
            self._spd_limit_editor.read()
            self._set_status("已请求读取 spd_limit")

    def _apply(self) -> None:
        dist = self._dist_spin.value()
        max_rpm = self._max_rpm.value()
        self._worker.request_set_distance(proto.build_set_distance(dist, max_rpm))
        self._set_status(f"SET_DISTANCE 已发送 Δs={dist:+d} mm max_rpm={max_rpm}")

    def _stop(self) -> None:
        self._worker.request_distance_stop()
        self._set_status("DISTANCE_STOP 已发送")

    def _step(self) -> None:
        self._dist_spin.setValue(self.DEFAULT_DIST_MM)
        self._apply()

    def _clear_plot(self) -> None:
        if self._on_clear_plot is not None:
            self._on_clear_plot()
        self._set_status("已清空距离曲线")
