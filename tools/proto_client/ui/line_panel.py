"""循迹环控制面板：启停 + 基准转速 + pid_line 快捷读写。"""

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


class LineControlPanel(QGroupBox):
    DEFAULT_BASE_RPM = 80

    def __init__(
        self,
        worker: SerialWorker,
        pid_line_editor: Optional[ParamEditor],
        line_base_editor: Optional[ParamEditor] = None,
        on_clear_plot: Optional[Callable[[], None]] = None,
        parent: Optional[QWidget] = None,
    ):
        super().__init__("循迹环控制", parent)
        self._worker = worker
        self._pid_line_editor = pid_line_editor
        self._line_base_editor = line_base_editor
        self._on_clear_plot = on_clear_plot
        self._running = False

        root = QVBoxLayout(self)

        if pid_line_editor is not None:
            root.addWidget(pid_line_editor)
        if line_base_editor is not None:
            root.addWidget(line_base_editor)

        form = QFormLayout()
        self._base_rpm = QSpinBox()
        self._base_rpm.setRange(0, 500)
        self._base_rpm.setSingleStep(10)
        self._base_rpm.setValue(self.DEFAULT_BASE_RPM)
        self._base_rpm.setSpecialValueText("用 NVS 默认")
        self._base_rpm.setSuffix(" RPM")
        form.addRow("本次覆盖基准", self._base_rpm)

        hint = QLabel(
            "开启后小车自主循迹（速度环内环 + pid_line 外环）。\n"
            "请先订阅「循迹 ADC」与「循迹环」观察曲线；丢线约 0.5 s 会自动停车。"
        )
        hint.setWordWrap(True)
        hint.setStyleSheet("color: palette(mid);")
        form.addRow(hint)

        btn_row = QHBoxLayout()
        self._btn_start = QPushButton("开启循迹")
        self._btn_stop = QPushButton("停止循迹")
        self._btn_clear = QPushButton("清空曲线")
        self._btn_start.setStyleSheet("font-weight: bold;")
        btn_row.addWidget(self._btn_start)
        btn_row.addWidget(self._btn_stop)
        btn_row.addWidget(self._btn_clear)
        form.addRow(btn_row)

        self._lbl_status = QLabel("就绪（未运行）")
        self._lbl_status.setWordWrap(True)
        form.addRow(self._lbl_status)
        root.addLayout(form)

        self._btn_start.clicked.connect(self._start)
        self._btn_stop.clicked.connect(self._stop)
        self._btn_clear.clicked.connect(self._clear_plot)

    def _set_status(self, text: str) -> None:
        self._lbl_status.setText(text)

    def set_running(self, running: bool) -> None:
        self._running = running
        self._btn_start.setEnabled(not running)
        self._set_status("循迹运行中…" if running else "就绪（未运行）")

    def _start(self) -> None:
        # 确保能看到 ADC + 环状态曲线
        mask = self._worker.optional_mask
        mask |= int(proto.TelChannel.LINE_ADC | proto.TelChannel.LINE_LOOP)
        self._worker.request_apply_subscription(mask)

        rpm = self._base_rpm.value()
        payload = proto.build_set_line_follow(None if rpm <= 0 else rpm)
        self._worker.request_set_line_follow(payload)
        self.set_running(True)
        self._set_status(f"已下发 SET_LINE_FOLLOW（base={rpm if rpm > 0 else 'NVS'}）")

    def _stop(self) -> None:
        self._worker.request_line_follow_stop()
        self.set_running(False)
        self._set_status("已下发 LINE_FOLLOW_STOP")

    def _clear_plot(self) -> None:
        if self._on_clear_plot is not None:
            self._on_clear_plot()
        self._set_status("已清空循迹曲线")
