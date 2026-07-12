"""姿态 / 循迹 / 编码器 / 转速 — 多视图监控与速度调试。"""

from __future__ import annotations

import time
from typing import Optional

import pyqtgraph as pg
import tm_proto as proto
from PySide6.QtCore import Qt
from PySide6.QtWidgets import (
    QFrame,
    QHBoxLayout,
    QLabel,
    QPushButton,
    QTabWidget,
    QVBoxLayout,
    QWidget,
)

from serial_worker import SerialWorker
from ui.param_panel import ParamEditor
from ui.speed_panel import SpeedControlPanel


def deg_to_180(deg: float) -> float:
    """归一化到 (-180, 180]。"""
    d = float(deg) % 360.0
    if d > 180.0:
        d -= 360.0
    if d <= -180.0:
        d += 360.0
    return d


class LineSensorStrip(QWidget):
    """五路循迹传感器：横向条带 + 0/1 状态指示。"""

    SENSOR_COLORS = ["#f1c40f", "#1abc9c", "#9b59b6", "#ecf0f1", "#e67e22"]

    def __init__(self, parent: Optional[QWidget] = None):
        super().__init__(parent)
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)

        hint = QLabel("左 ←  L1 … L5  → 右   （1=检测到线，0=未检测）")
        hint.setStyleSheet("color: #666; font-size: 11px;")
        layout.addWidget(hint)

        row = QHBoxLayout()
        row.setSpacing(12)
        self._cells: list[QFrame] = []
        self._value_labels: list[QLabel] = []

        for i in range(proto.LINE_SENSOR_COUNT):
            cell = QFrame()
            cell.setFrameShape(QFrame.Shape.StyledPanel)
            cell.setMinimumSize(72, 88)
            cell.setMaximumWidth(100)
            cell_layout = QVBoxLayout(cell)
            name = QLabel(f"L{i + 1}")
            name.setAlignment(Qt.AlignmentFlag.AlignCenter)
            name.setStyleSheet("font-weight: bold;")
            val = QLabel("—")
            val.setAlignment(Qt.AlignmentFlag.AlignCenter)
            val.setStyleSheet("font-size: 18px;")
            cell_layout.addWidget(name)
            cell_layout.addStretch()
            cell_layout.addWidget(val)
            cell_layout.addStretch()
            row.addWidget(cell, stretch=1)
            self._cells.append(cell)
            self._value_labels.append(val)

        layout.addLayout(row)

    def update_values(self, values: tuple[int, ...]) -> None:
        for i, val in enumerate(values[: len(self._cells)]):
            on = int(val) != 0
            self._value_labels[i].setText("1" if on else "0")
            color = self.SENSOR_COLORS[i % len(self.SENSOR_COLORS)]
            if on:
                bg = color
                fg = "#1a1a1a"
                border = color
            else:
                bg = "#2b2b2b"
                fg = "#888"
                border = "#444"
            self._cells[i].setStyleSheet(
                f"QFrame {{ background: {bg}; border: 2px solid {border}; border-radius: 8px; }}"
                f"QLabel {{ color: {fg}; background: transparent; }}"
            )


class PlotTab(QWidget):
    HISTORY = 600
    TIME_WINDOW_S = 60.0
    MOTOR_COLORS = ["#e74c3c", "#3498db", "#2ecc71", "#f39c12"]
    MOTOR_NAMES = ["M1", "M2", "M3", "M4"]

    def __init__(
        self,
        worker: SerialWorker,
        pid_editor: Optional[ParamEditor] = None,
        spd_limit_editor: Optional[ParamEditor] = None,
        parent: Optional[QWidget] = None,
    ):
        super().__init__(parent)
        self._time_origin: Optional[float] = None
        self._charts_paused = False
        self._last_att = (0.0, 0.0, 0.0)

        root = QVBoxLayout(self)
        root.addWidget(self._build_toolbar())
        self._views = QTabWidget()
        root.addWidget(self._views)

        self._build_attitude_view()
        self._build_line_view()
        self._build_encoder_view()
        self._build_rpm_view(worker, pid_editor, spd_limit_editor)

    def _build_toolbar(self) -> QWidget:
        bar = QWidget()
        row = QHBoxLayout(bar)
        row.setContentsMargins(0, 0, 0, 4)
        self._btn_pause = QPushButton("暂停绘图")
        self._btn_pause.setCheckable(True)
        self._btn_pause.toggled.connect(self._on_pause_toggled)
        self._btn_follow = QPushButton("跟随最新")
        self._btn_follow.clicked.connect(self._follow_latest_all)
        self._lbl_pause = QLabel("绘图运行中（通信不受影响）")
        self._lbl_pause.setStyleSheet("color: #666;")
        row.addWidget(self._btn_pause)
        row.addWidget(self._btn_follow)
        row.addWidget(self._lbl_pause, stretch=1)
        return bar

    def _on_pause_toggled(self, paused: bool) -> None:
        self._charts_paused = paused
        self._btn_pause.setText("继续绘图" if paused else "暂停绘图")
        if paused:
            self._lbl_pause.setText("绘图已暂停（后台仍接收数据）")
            self._lbl_pause.setStyleSheet("color: #c8860a; font-weight: bold;")
        else:
            self._lbl_pause.setText("绘图运行中（通信不受影响）")
            self._lbl_pause.setStyleSheet("color: #666;")
            self._redraw_all()

    def _follow_latest_all(self) -> None:
        self._redraw_all()

    def _build_attitude_view(self) -> None:
        page = QWidget()
        layout = QVBoxLayout(page)
        self._lbl_att_live = QLabel("roll=—  pitch=—  yaw=—  (°)")
        layout.addWidget(self._lbl_att_live)

        self._plot_rp = pg.PlotWidget(title="roll / pitch / yaw (-180~180°)")
        self._plot_rp.showGrid(x=True, y=True, alpha=0.3)
        self._plot_rp.setLabel("left", "deg")
        self._plot_rp.setLabel("bottom", "时间", units="s")
        self._plot_rp.setYRange(-180, 180)
        self._plot_rp.setLimits(yMin=-180, yMax=180)
        self._plot_rp.getViewBox().setMouseEnabled(y=False)
        legend = self._plot_rp.addLegend(offset=(10, 10))
        self._curve_roll = self._plot_rp.plot(pen=pg.mkPen("#e74c3c", width=2))
        self._curve_pitch = self._plot_rp.plot(pen=pg.mkPen("#2ecc71", width=2))
        self._curve_yaw = self._plot_rp.plot(pen=pg.mkPen("#3498db", width=2))
        legend.addItem(self._curve_roll, "roll")
        legend.addItem(self._curve_pitch, "pitch")
        legend.addItem(self._curve_yaw, "yaw")
        layout.addWidget(self._plot_rp)

        self._att_time: list[float] = []
        self._att_roll: list[float] = []
        self._att_pitch: list[float] = []
        self._att_yaw: list[float] = []
        self._views.addTab(page, "姿态")

    def _build_line_view(self) -> None:
        page = QWidget()
        layout = QVBoxLayout(page)

        self._line_strip = LineSensorStrip()
        layout.addWidget(self._line_strip)

        self._plot_line = pg.PlotWidget(title="循迹历史 (0/1)")
        self._plot_line.showGrid(x=True, y=True, alpha=0.3)
        self._plot_line.setLabel("left", "on-line")
        self._plot_line.setLabel("bottom", "时间", units="s")
        self._plot_line.setYRange(-0.1, 1.2)
        self._plot_line.setLimits(yMin=-0.2, yMax=1.3)
        legend = self._plot_line.addLegend(offset=(10, 10))
        self._line_curves = []
        for i, color in enumerate(LineSensorStrip.SENSOR_COLORS[: proto.LINE_SENSOR_COUNT]):
            curve = self._plot_line.plot(pen=pg.mkPen(color, width=2), name=f"L{i + 1}")
            self._line_curves.append(curve)
            legend.addItem(curve, f"L{i + 1}")
        layout.addWidget(self._plot_line, stretch=1)

        self._line_time: list[float] = []
        self._line_data: list[list[float]] = [[] for _ in range(proto.LINE_SENSOR_COUNT)]
        self._line_last: tuple[int, ...] = tuple(0 for _ in range(proto.LINE_SENSOR_COUNT))
        self._views.addTab(page, "循迹")

    def _build_encoder_view(self) -> None:
        page = QWidget()
        layout = QVBoxLayout(page)

        self._plot_enc = pg.PlotWidget(title="编码器计数")
        self._plot_enc.addLegend(offset=(10, 10))
        self._plot_enc.setLabel("left", "counts")
        self._plot_enc.setLabel("bottom", "时间", units="s")
        self._plot_enc.showGrid(x=True, y=True, alpha=0.3)
        self._enc_curves = []
        for i, name in enumerate(self.MOTOR_NAMES):
            curve = self._plot_enc.plot(
                pen=pg.mkPen(self.MOTOR_COLORS[i], width=2), name=name
            )
            self._enc_curves.append(curve)
        layout.addWidget(self._plot_enc, stretch=1)

        self._lbl_enc_live = QLabel("M1=—  M2=—  M3=—  M4=—")
        layout.addWidget(self._lbl_enc_live)

        self._enc_time: list[float] = []
        self._enc_data: list[list[float]] = [[] for _ in range(4)]
        self._views.addTab(page, "编码器")

    def _build_rpm_view(
        self,
        worker: SerialWorker,
        pid_editor: Optional[ParamEditor],
        spd_limit_editor: Optional[ParamEditor],
    ) -> None:
        page = QWidget()
        layout = QHBoxLayout(page)

        chart_col = QVBoxLayout()
        self._plot_rpm = pg.PlotWidget(title="轮速 RPM（需订阅「电机 RPM」）")
        self._plot_rpm.addLegend(offset=(10, 10))
        self._plot_rpm.setLabel("left", "RPM")
        self._plot_rpm.setLabel("bottom", "时间", units="s")
        self._plot_rpm.showGrid(x=True, y=True, alpha=0.3)
        self._rpm_curves = []
        self._target_lines = []
        for i, name in enumerate(self.MOTOR_NAMES):
            curve = self._plot_rpm.plot(
                pen=pg.mkPen(self.MOTOR_COLORS[i], width=2), name=name
            )
            self._rpm_curves.append(curve)
            line = pg.InfiniteLine(
                angle=0,
                pen=pg.mkPen(self.MOTOR_COLORS[i], width=1, style=Qt.PenStyle.DashLine),
            )
            line.setVisible(False)
            self._plot_rpm.addItem(line)
            self._target_lines.append(line)
        chart_col.addWidget(self._plot_rpm, stretch=1)

        self._lbl_rpm_live = QLabel("M1=—  M2=—  M3=—  M4=—")
        chart_col.addWidget(self._lbl_rpm_live)
        layout.addLayout(chart_col, stretch=3)

        self._speed_panel = SpeedControlPanel(
            worker,
            pid_editor,
            spd_limit_editor,
            on_clear_plot=self.reset_rpm_plot,
        )
        self._speed_panel.set_target_line_callback(self._update_target_lines)
        self._speed_panel.setMaximumWidth(360)
        layout.addWidget(self._speed_panel, stretch=1)

        self._rpm_time: list[float] = []
        self._rpm_data: list[list[float]] = [[] for _ in range(4)]
        self._views.addTab(page, "转速 / 调试")

    def reset(self) -> None:
        self._time_origin = None
        self._charts_paused = False
        self._btn_pause.setChecked(False)
        self._btn_pause.setText("暂停绘图")
        self._lbl_pause.setText("绘图运行中（通信不受影响）")
        self._lbl_pause.setStyleSheet("color: #666;")

        self._att_time.clear()
        self._att_roll.clear()
        self._att_pitch.clear()
        self._att_yaw.clear()
        self._last_att = (0.0, 0.0, 0.0)
        self._lbl_att_live.setText("roll=—  pitch=—  yaw=—  (°)")

        self._line_time.clear()
        for series in self._line_data:
            series.clear()
        self._line_last = tuple(0 for _ in range(proto.LINE_SENSOR_COUNT))

        self.reset_encoder_plot()
        self.reset_rpm_plot()
        self._lbl_enc_live.setText("M1=—  M2=—  M3=—  M4=—")
        self._lbl_rpm_live.setText("M1=—  M2=—  M3=—  M4=—")
        self._redraw_all()

    def reset_encoder_plot(self) -> None:
        self._enc_time.clear()
        for series in self._enc_data:
            series.clear()
        if not self._charts_paused:
            self._redraw_encoder()

    def reset_rpm_plot(self) -> None:
        self._rpm_time.clear()
        for series in self._rpm_data:
            series.clear()
        if not self._charts_paused:
            self._redraw_rpm()

    def _plot_time_s(self) -> float:
        if self._time_origin is None:
            self._time_origin = time.monotonic()
        return time.monotonic() - self._time_origin

    def _trim_series(self, time_buf: list[float], *series_bufs: list) -> None:
        while len(time_buf) > self.HISTORY:
            time_buf.pop(0)
            for buf in series_bufs:
                if buf:
                    buf.pop(0)

    def _set_time_window(self, plot: pg.PlotWidget, time_buf: list[float]) -> None:
        if not time_buf:
            return
        t_max = time_buf[-1]
        t_min = max(0.0, t_max - self.TIME_WINDOW_S)
        plot.setXRange(t_min, t_max, padding=0.02)

    def _redraw_attitude(self) -> None:
        self._curve_roll.setData(self._att_time, self._att_roll)
        self._curve_pitch.setData(self._att_time, self._att_pitch)
        self._curve_yaw.setData(self._att_time, self._att_yaw)
        self._set_time_window(self._plot_rp, self._att_time)
        r, p, y = self._last_att
        self._lbl_att_live.setText(
            f"roll={r:.0f}  pitch={p:.0f}  yaw={y:.0f}  (°)"
        )

    def _redraw_line(self) -> None:
        self._line_strip.update_values(self._line_last)
        for i, curve in enumerate(self._line_curves):
            n = min(len(self._line_time), len(self._line_data[i]))
            if n > 0:
                curve.setData(self._line_time[-n:], self._line_data[i][-n:])
            else:
                curve.setData([], [])
        self._set_time_window(self._plot_line, self._line_time)

    def _redraw_encoder(self) -> None:
        n = min(len(self._enc_time), *(len(s) for s in self._enc_data)) if self._enc_time else 0
        xs = self._enc_time[-n:] if n else []
        for i, curve in enumerate(self._enc_curves):
            curve.setData(xs, self._enc_data[i][-n:] if n else [])
        self._set_time_window(self._plot_enc, self._enc_time)

    def _redraw_rpm(self) -> None:
        n = min(len(self._rpm_time), *(len(s) for s in self._rpm_data)) if self._rpm_time else 0
        xs = self._rpm_time[-n:] if n else []
        for i, curve in enumerate(self._rpm_curves):
            curve.setData(xs, self._rpm_data[i][-n:] if n else [])
        self._set_time_window(self._plot_rpm, self._rpm_time)

    def _redraw_all(self) -> None:
        self._redraw_attitude()
        self._redraw_line()
        self._redraw_encoder()
        self._redraw_rpm()

    def update_attitude(self, roll: float, pitch: float, yaw: float) -> None:
        r = deg_to_180(roll)
        p = deg_to_180(pitch)
        y = deg_to_180(yaw)
        self._last_att = (r, p, y)

        t = self._plot_time_s()
        self._att_time.append(t)
        self._att_roll.append(r)
        self._att_pitch.append(p)
        self._att_yaw.append(y)
        self._trim_series(self._att_time, self._att_roll, self._att_pitch, self._att_yaw)

        if not self._charts_paused:
            self._redraw_attitude()
        else:
            self._lbl_att_live.setText(
                f"roll={r:.0f}  pitch={p:.0f}  yaw={y:.0f}  (°)  [暂停]"
            )

    def update_line_adc(self, values: tuple[int, ...]) -> None:
        self._line_last = values
        t = self._plot_time_s()
        self._line_time.append(t)
        if len(self._line_time) > self.HISTORY:
            self._line_time.pop(0)
        for i, val in enumerate(values[: len(self._line_curves)]):
            self._line_data[i].append(float(val))
            if len(self._line_data[i]) > self.HISTORY:
                self._line_data[i].pop(0)
        if not self._charts_paused:
            self._redraw_line()

    def on_encoder_counts(self, counts: tuple[int, int, int, int]) -> None:
        self._lbl_enc_live.setText(
            "  ".join(f"{self.MOTOR_NAMES[i]}={counts[i]}" for i in range(4))
        )
        t = self._plot_time_s()
        self._enc_time.append(t)
        for i in range(4):
            self._enc_data[i].append(float(counts[i]))
        self._trim_series(self._enc_time, *self._enc_data)
        if not self._charts_paused:
            self._redraw_encoder()

    def on_motor_rpm(self, rpms: tuple[int, int, int, int]) -> None:
        self._lbl_rpm_live.setText(
            "  ".join(f"{self.MOTOR_NAMES[i]}={rpms[i]}" for i in range(4))
        )
        t = self._plot_time_s()
        self._rpm_time.append(t)
        for i in range(4):
            self._rpm_data[i].append(float(rpms[i]))
        self._trim_series(self._rpm_time, *self._rpm_data)
        if not self._charts_paused:
            self._redraw_rpm()

    def _update_target_lines(self, target: list[int], mode: int, motor_id: int) -> None:
        for i, line in enumerate(self._target_lines):
            line.setValue(float(target[i]))
            if mode == 0:
                line.setVisible(motor_id == i + 1)
            else:
                line.setVisible(True)
