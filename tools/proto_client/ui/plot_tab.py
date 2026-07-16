"""姿态 / 循迹 / 编码器 / 转速 — 多视图监控与速度调试。"""

from __future__ import annotations

import time
from typing import Optional

import pyqtgraph as pg
import tm_proto as proto
from PySide6.QtCore import Qt
from PySide6.QtWidgets import (
    QCheckBox,
    QComboBox,
    QFrame,
    QHBoxLayout,
    QLabel,
    QPushButton,
    QTabWidget,
    QVBoxLayout,
    QWidget,
)

from angle_log_export import AngleLoopRecorder, log_dir
from serial_worker import SerialWorker
from ui.angle_panel import AngleControlPanel
from ui.distance_panel import DistanceControlPanel
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


def angle_plot_y_range(values: list[float]) -> tuple[float, float]:
    """根据本次机动数据自适应 Y 轴，保证 ±180 大转角也可见。"""
    if not values:
        return -30.0, 150.0
    lo = min(values)
    hi = max(values)
    span = hi - lo
    margin = max(8.0, span * 0.12)
    if span < 20.0:
        mid = (lo + hi) * 0.5
        lo = mid - 10.0
        hi = mid + 10.0
    else:
        lo -= margin
        hi += margin
    return lo, hi


def distance_plot_y_range(values: list[float]) -> tuple[float, float]:
    """距离曲线 Y 轴自适应（mm）。"""
    if not values:
        return -100.0, 600.0
    lo = min(values)
    hi = max(values)
    span = hi - lo
    margin = max(20.0, span * 0.12)
    if span < 40.0:
        mid = (lo + hi) * 0.5
        lo = mid - 20.0
        hi = mid + 20.0
    else:
        lo -= margin
        hi += margin
    return lo, hi


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
    # 半透明 RGB 元组：重叠曲线可透过上层看到下层
    _MOTOR_RGBA = [
        (231, 76, 60, 130),    # M1 #e74c3c
        (52, 152, 219, 130),   # M2 #3498db
        (46, 204, 113, 130),   # M3 #2ecc71
        (243, 156, 18, 130),   # M4 #f39c12
    ]
    MOTOR_NAMES = ["M1", "M2", "M3", "M4"]

    def __init__(
        self,
        worker: SerialWorker,
        pid_editor: Optional[ParamEditor] = None,
        spd_limit_editor: Optional[ParamEditor] = None,
        pid_yaw_editor: Optional[ParamEditor] = None,
        pid_dist_editor: Optional[ParamEditor] = None,
        parent: Optional[QWidget] = None,
    ):
        super().__init__(parent)
        self._time_origin: Optional[float] = None
        self._charts_paused = False
        self._last_att = (0.0, 0.0, 0.0)
        self._motor_count = proto.MOTOR_COUNT_DEFAULT
        self._rpm_subscribed = False
        self._motor_visible = [True] * proto.MOTOR_COUNT_MAX
        self._rpm_targets = [0] * proto.MOTOR_COUNT_MAX
        self._rpm_target_mode = 0
        self._rpm_target_motor_id = 1
        self._last_rpms = [0] * proto.MOTOR_COUNT_MAX
        self._angle_subscribed = False
        self._angle_target_yaw = 0
        self._angle_current_yaw: Optional[int] = None
        self._angle_turn_rpm = 0
        self._angle_base_rpm = 0
        self._angle_recorder = AngleLoopRecorder()
        self._distance_subscribed = False
        self._dist_target_mm = 0
        self._dist_current_mm: Optional[int] = None
        self._dist_cmd_rpm = 0
        self._dist_max_rpm = 0

        root = QVBoxLayout(self)
        root.addWidget(self._build_toolbar())
        self._views = QTabWidget()
        root.addWidget(self._views)

        self._build_attitude_view()
        self._build_line_view()
        self._build_encoder_view()
        self._build_rpm_view(worker, pid_editor, spd_limit_editor)
        self._build_angle_view(worker, pid_yaw_editor, spd_limit_editor)
        self._build_distance_view(worker, pid_dist_editor, spd_limit_editor)

    def _build_distance_view(
        self,
        worker: SerialWorker,
        pid_dist_editor: Optional[ParamEditor],
        spd_limit_editor: Optional[ParamEditor],
    ) -> None:
        page = QWidget()
        layout = QHBoxLayout(page)

        chart_col = QVBoxLayout()

        self._lbl_distance_hint = QLabel("等待订阅距离环…")
        self._lbl_distance_hint.setStyleSheet("color: #c8860a;")
        chart_col.addWidget(self._lbl_distance_hint)

        motor_bar = QHBoxLayout()
        motor_bar.addWidget(QLabel("车型"))
        self._distance_motor_count_combo = QComboBox()
        self._fill_motor_count_combo(self._distance_motor_count_combo)
        self._distance_motor_count_combo.currentIndexChanged.connect(self._on_distance_motor_count_combo)
        motor_bar.addWidget(self._distance_motor_count_combo)
        motor_bar.addStretch()
        chart_col.addLayout(motor_bar)

        self._plot_distance = pg.PlotWidget(title="本次相对位移 (mm)")
        dist_legend = self._plot_distance.addLegend(offset=(10, 10))
        self._plot_distance.setLabel("left", "mm")
        self._plot_distance.setLabel("bottom", "时间", units="s")
        self._plot_distance.showGrid(x=True, y=True, alpha=0.3)
        self._plot_distance.setYRange(-100, 600)
        self._plot_distance.getViewBox().setMouseEnabled(y=False)
        self._curve_dist_meas = self._plot_distance.plot(pen=pg.mkPen("#1abc9c", width=2))
        self._curve_dist_target = self._plot_distance.plot(
            pen=pg.mkPen("#e67e22", width=2, style=Qt.PenStyle.DashLine),
        )
        dist_legend.addItem(self._curve_dist_meas, "当前 Δs")
        dist_legend.addItem(self._curve_dist_target, "目标 Δs")
        chart_col.addWidget(self._plot_distance, stretch=1)

        self._lbl_distance_live = QLabel("目标=—  当前=—  cmd=—  max=—")
        chart_col.addWidget(self._lbl_distance_live)
        layout.addLayout(chart_col, stretch=3)

        self._distance_panel = DistanceControlPanel(
            worker,
            pid_dist_editor,
            spd_limit_editor,
            on_clear_plot=self.reset_distance_plot,
        )
        self._distance_panel.setMaximumWidth(360)
        layout.addWidget(self._distance_panel, stretch=1)

        self._distance_time: list[float] = []
        self._distance_meas: list[float] = []
        self._distance_target_series: list[float] = []
        self._views.addTab(page, "距离 / 调试")

    @staticmethod
    def _fill_motor_count_combo(combo: QComboBox) -> None:
        combo.clear()
        combo.addItem("两轮 (M1~M2)", proto.MOTOR_COUNT_DEFAULT)
        combo.addItem("四轮 (M1~M4)", proto.MOTOR_COUNT_MAX)

    def _on_distance_motor_count_combo(self) -> None:
        count = self._distance_motor_count_combo.currentData()
        if count is None:
            return
        self.set_motor_count(int(count))

    def set_distance_subscribed(self, subscribed: bool) -> None:
        self._distance_subscribed = subscribed
        if subscribed:
            self._lbl_distance_hint.setText(
                "距离环已订阅 — 实线=当前位移，橙色虚线=目标位移"
            )
            self._lbl_distance_hint.setStyleSheet("color: #2d7a2d;")
        else:
            self._lbl_distance_hint.setText(
                "未订阅距离环：在仪表盘勾选「距离环」并点「应用订阅」"
            )
            self._lbl_distance_hint.setStyleSheet("color: #c8860a; font-weight: bold;")

    def reset_distance_plot(self) -> None:
        self._distance_time.clear()
        self._distance_meas.clear()
        self._distance_target_series.clear()
        self._dist_current_mm = None
        self._dist_cmd_rpm = 0
        self._dist_max_rpm = 0
        if not self._charts_paused:
            self._redraw_distance()
        else:
            self._refresh_distance_live_label()

    def _refresh_distance_live_label(self) -> None:
        cur = "—" if self._dist_current_mm is None else str(self._dist_current_mm)
        suffix = "  [暂停]" if self._charts_paused else ""
        self._lbl_distance_live.setText(
            f"目标={self._dist_target_mm} mm  当前={cur} mm  "
            f"cmd={self._dist_cmd_rpm}  max={self._dist_max_rpm}{suffix}"
        )

    def _redraw_distance(self) -> None:
        n = min(len(self._distance_time), len(self._distance_meas), len(self._distance_target_series))
        xs = self._distance_time[-n:] if n else []
        if n:
            meas = self._distance_meas[-n:]
            tgt = self._distance_target_series[-n:]
            self._curve_dist_meas.setData(xs, meas)
            self._curve_dist_target.setData(xs, tgt)
            y_lo, y_hi = distance_plot_y_range(meas + tgt)
            self._plot_distance.setYRange(y_lo, y_hi, padding=0)
        else:
            self._curve_dist_meas.setData([], [])
            self._curve_dist_target.setData([], [])
            self._plot_distance.setYRange(-100, 600, padding=0)
        self._set_time_window(self._plot_distance, self._distance_time)
        self._refresh_distance_live_label()

    def on_distance_loop(self, sample: proto.DistanceLoopPush) -> None:
        self._dist_current_mm = sample.current_mm
        self._dist_target_mm = sample.target_mm
        self._dist_cmd_rpm = sample.cmd_rpm
        self._dist_max_rpm = sample.max_rpm
        t = self._plot_time_s()
        self._distance_time.append(t)
        self._distance_meas.append(float(sample.current_mm))
        self._distance_target_series.append(float(sample.target_mm))
        self._trim_series(self._distance_time, self._distance_meas, self._distance_target_series)
        if not self._charts_paused:
            self._redraw_distance()
        else:
            self._refresh_distance_live_label()

    def _build_angle_view(
        self,
        worker: SerialWorker,
        pid_yaw_editor: Optional[ParamEditor],
        spd_limit_editor: Optional[ParamEditor],
    ) -> None:
        page = QWidget()
        layout = QHBoxLayout(page)

        chart_col = QVBoxLayout()

        self._lbl_angle_hint = QLabel("等待订阅角度环…")
        self._lbl_angle_hint.setStyleSheet("color: #c8860a;")
        chart_col.addWidget(self._lbl_angle_hint)

        motor_bar = QHBoxLayout()
        motor_bar.addWidget(QLabel("车型"))
        self._angle_motor_count_combo = QComboBox()
        self._fill_motor_count_combo(self._angle_motor_count_combo)
        self._angle_motor_count_combo.currentIndexChanged.connect(self._on_angle_motor_count_combo)
        motor_bar.addWidget(self._angle_motor_count_combo)
        motor_bar.addStretch()
        chart_col.addLayout(motor_bar)

        self._plot_angle = pg.PlotWidget(title="本次相对转角 (°)")
        angle_legend = self._plot_angle.addLegend(offset=(10, 10))
        self._plot_angle.setLabel("left", "deg")
        self._plot_angle.setLabel("bottom", "时间", units="s")
        self._plot_angle.showGrid(x=True, y=True, alpha=0.3)
        self._plot_angle.setYRange(-30, 150)
        self._plot_angle.getViewBox().setMouseEnabled(y=False)
        self._curve_yaw_meas = self._plot_angle.plot(pen=pg.mkPen("#3498db", width=2))
        self._curve_yaw_target = self._plot_angle.plot(
            pen=pg.mkPen("#e67e22", width=2, style=Qt.PenStyle.DashLine),
        )
        angle_legend.addItem(self._curve_yaw_meas, "当前 Δθ")
        angle_legend.addItem(self._curve_yaw_target, "目标 Δθ")
        chart_col.addWidget(self._plot_angle, stretch=1)

        self._lbl_angle_live = QLabel("目标=—  当前=—  turn=—  base=—")
        chart_col.addWidget(self._lbl_angle_live)
        layout.addLayout(chart_col, stretch=3)

        self._angle_panel = AngleControlPanel(
            worker,
            pid_yaw_editor,
            spd_limit_editor,
            on_clear_plot=self.reset_angle_plot,
            on_log_start=self.start_angle_log,
            on_log_export=self.export_angle_log,
            on_log_command=self.log_angle_command,
            on_get_recorder=self._get_angle_recorder,
            on_get_pid_params=self._get_angle_pid_params,
        )
        # 目标曲线由固件 ANGLE_LOOP 推送的绝对航向更新，不再绑定 spin 框
        self._angle_panel.setMaximumWidth(360)
        layout.addWidget(self._angle_panel, stretch=1)

        self._angle_time: list[float] = []
        self._angle_meas: list[float] = []
        self._angle_target_series: list[float] = []
        self._views.addTab(page, "角度 / 调试")
        idx = self._angle_motor_count_combo.findData(self._motor_count)
        if idx >= 0:
            self._angle_motor_count_combo.setCurrentIndex(idx)
        self._angle_panel.set_motor_count(self._motor_count)

    def _on_angle_motor_count_combo(self) -> None:
        count = self._angle_motor_count_combo.currentData()
        if count is None:
            return
        self.set_motor_count(int(count))

    def set_angle_subscribed(self, subscribed: bool) -> None:
        self._angle_subscribed = subscribed
        if subscribed:
            self._lbl_angle_hint.setText(
                "角度环已订阅 — 实线=当前 yaw，橙色虚线=目标 yaw"
            )
            self._lbl_angle_hint.setStyleSheet("color: #2d7a2d;")
        else:
            self._lbl_angle_hint.setText(
                "未订阅角度环：在仪表盘勾选「角度环」并点「应用订阅」"
            )
            self._lbl_angle_hint.setStyleSheet("color: #c8860a; font-weight: bold;")

    def _update_angle_target_line(self, target_yaw: int) -> None:
        self._angle_target_yaw = int(target_yaw)
        if not self._charts_paused:
            self._redraw_angle()
        else:
            self._refresh_angle_live_label()

    def reset_angle_plot(self) -> None:
        self._angle_time.clear()
        self._angle_meas.clear()
        self._angle_target_series.clear()
        self._angle_current_yaw = None
        self._angle_turn_rpm = 0
        self._angle_base_rpm = 0
        if not self._charts_paused:
            self._redraw_angle()
        else:
            self._refresh_angle_live_label()

    def _refresh_angle_live_label(self) -> None:
        cur = "—" if self._angle_current_yaw is None else str(self._angle_current_yaw)
        suffix = "  [暂停]" if self._charts_paused else ""
        self._lbl_angle_live.setText(
            f"目标={self._angle_target_yaw}°  当前={cur}°  "
            f"turn={self._angle_turn_rpm}  base={self._angle_base_rpm}{suffix}"
        )

    def _redraw_angle(self) -> None:
        n = min(len(self._angle_time), len(self._angle_meas), len(self._angle_target_series))
        xs = self._angle_time[-n:] if n else []
        if n:
            meas = self._angle_meas[-n:]
            tgt = self._angle_target_series[-n:]
            self._curve_yaw_meas.setData(xs, meas)
            self._curve_yaw_target.setData(xs, tgt)
            y_lo, y_hi = angle_plot_y_range(meas + tgt)
            self._plot_angle.setYRange(y_lo, y_hi, padding=0)
        else:
            self._curve_yaw_meas.setData([], [])
            self._curve_yaw_target.setData([], [])
            self._plot_angle.setYRange(-30, 150, padding=0)
        self._set_time_window(self._plot_angle, self._angle_time)
        self._refresh_angle_live_label()

    def start_angle_log(self) -> str:
        self._angle_recorder.start(
            self._motor_count,
            self._angle_subscribed,
            self._rpm_subscribed,
        )
        return str(log_dir().resolve())

    def export_angle_log(self) -> str:
        self._angle_recorder.stop()
        csv_path, meta_path = self._angle_recorder.export()
        return f"{csv_path.name}, {meta_path.name}"

    def log_angle_command(
        self,
        name: str,
        *,
        target: Optional[int] = None,
        base: Optional[int] = None,
        max_turn: Optional[int] = None,
        note: str = "",
    ) -> None:
        self._angle_recorder.record_command(
            name,
            target=target,
            base=base,
            max_turn=max_turn,
            note=note,
        )

    def angle_log_is_recording(self) -> bool:
        return self._angle_recorder.active

    def _get_angle_recorder(self) -> object:
        """供 AngleControlPanel 获取当前 Recorder。"""
        return self._angle_recorder

    def _get_angle_pid_params(self) -> dict:
        """返回当前已知的 PID yaw 参数（从编辑器 UI 控件读取）。"""
        params: dict = {}
        # 尝试从编辑器 UI 控件获取
        if hasattr(self, "_angle_panel") and self._angle_panel._pid_yaw_editor is not None:
            editor = self._angle_panel._pid_yaw_editor
            try:
                fields = getattr(editor, "_fields", {})
                for key in ("kp", "ki", "kd"):
                    widget = fields.get(key)
                    if widget is not None:
                        text = widget.text().strip()
                        if text:
                            params[key] = float(text)
            except Exception:
                pass
        # 从 spd_limit 获取 max_rpm
        if hasattr(self, "_angle_panel") and self._angle_panel._spd_limit_editor is not None:
            editor = self._angle_panel._spd_limit_editor
            try:
                fields = getattr(editor, "_fields", {})
                widget = fields.get("max_rpm")
                if widget is not None:
                    text = widget.text().strip()
                    if text:
                        params["max_rpm"] = float(text)
            except Exception:
                pass
        return params

    def on_angle_loop(self, sample: proto.AngleLoopPush) -> None:
        self._angle_current_yaw = sample.current_yaw
        self._angle_target_yaw = sample.target_yaw
        self._angle_turn_rpm = sample.turn_rpm
        self._angle_base_rpm = sample.base_rpm
        yaw_meas = deg_to_180(sample.current_yaw)
        yaw_tgt = deg_to_180(sample.target_yaw)
        self._angle_recorder.record_angle_loop(sample, yaw_meas, yaw_tgt)
        t = self._plot_time_s()
        self._angle_time.append(t)
        self._angle_meas.append(float(yaw_meas))
        self._angle_target_series.append(float(yaw_tgt))
        self._trim_series(self._angle_time, self._angle_meas, self._angle_target_series)
        if not self._charts_paused:
            self._redraw_angle()
        else:
            self._refresh_angle_live_label()

    def on_attitude_yaw_for_angle(self, yaw: float) -> None:
        """角度环未推送时用姿态 yaw 填充当前曲线。"""
        if self._angle_subscribed:
            return
        y = deg_to_180(yaw)
        self._angle_current_yaw = int(round(y))
        t = self._plot_time_s()
        self._angle_time.append(t)
        self._angle_meas.append(y)
        self._angle_target_series.append(float(deg_to_180(self._angle_target_yaw)))
        self._trim_series(self._angle_time, self._angle_meas, self._angle_target_series)
        if not self._charts_paused:
            self._redraw_angle()

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
        enc_legend = self._plot_enc.addLegend(offset=(10, 10))
        self._plot_enc.setLabel("left", "counts")
        self._plot_enc.setLabel("bottom", "时间", units="s")
        self._plot_enc.showGrid(x=True, y=True, alpha=0.3)
        self._enc_curves = []
        for i, name in enumerate(self.MOTOR_NAMES):
            curve = self._plot_enc.plot(pen=pg.mkPen(self.MOTOR_COLORS[i], width=2))
            enc_legend.addItem(curve, name)
            self._enc_curves.append(curve)
        layout.addWidget(self._plot_enc, stretch=1)

        self._lbl_enc_live = QLabel("M1=—  M2=—  M3=—  M4=—")
        layout.addWidget(self._lbl_enc_live)

        self._enc_time: list[float] = []
        self._enc_data: list[list[float]] = [[] for _ in range(proto.MOTOR_COUNT_MAX)]
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

        self._lbl_rpm_hint = QLabel("等待订阅电机 RPM…")
        self._lbl_rpm_hint.setStyleSheet("color: #c8860a;")
        chart_col.addWidget(self._lbl_rpm_hint)

        motor_bar = QHBoxLayout()
        motor_bar.addWidget(QLabel("车型"))
        self._motor_count_combo = QComboBox()
        self._fill_motor_count_combo(self._motor_count_combo)
        self._motor_count_combo.currentIndexChanged.connect(self._on_motor_count_combo)
        motor_bar.addWidget(self._motor_count_combo)
        motor_bar.addSpacing(12)
        motor_bar.addWidget(QLabel("显示曲线"))
        self._motor_checks: list[QCheckBox] = []
        for i, name in enumerate(self.MOTOR_NAMES):
            chk = QCheckBox(name)
            chk.setChecked(True)
            chk.toggled.connect(lambda checked, idx=i: self._on_motor_visibility(idx, checked))
            motor_bar.addWidget(chk)
            self._motor_checks.append(chk)
        motor_bar.addStretch()
        chart_col.addLayout(motor_bar)

        self._plot_rpm = pg.PlotWidget(title="轮速 RPM")
        rpm_legend = self._plot_rpm.addLegend(offset=(10, 10))
        self._plot_rpm.setLabel("left", "RPM")
        self._plot_rpm.setLabel("bottom", "时间", units="s")
        self._plot_rpm.showGrid(x=True, y=True, alpha=0.3)
        self._plot_rpm.enableAutoRange(axis="y")
        self._rpm_curves = []
        self._target_curves = []
        for i, name in enumerate(self.MOTOR_NAMES):
            rgba = self._MOTOR_RGBA[i]
            curve = self._plot_rpm.plot(pen=pg.mkPen(rgba, width=2))
            rpm_legend.addItem(curve, f"{name} 实测")
            self._rpm_curves.append(curve)
            tgt_curve = self._plot_rpm.plot(
                pen=pg.mkPen(rgba, width=2, style=Qt.PenStyle.DashLine),
            )
            rpm_legend.addItem(tgt_curve, f"{name} 目标")
            self._target_curves.append(tgt_curve)
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
        self._rpm_data: list[list[float]] = [[] for _ in range(proto.MOTOR_COUNT_MAX)]
        self._views.addTab(page, "转速 / 调试")
        self.set_motor_count(proto.MOTOR_COUNT_DEFAULT)

    def set_motor_count(self, count: int) -> None:
        count = max(2, min(proto.MOTOR_COUNT_MAX, int(count)))
        if count == self._motor_count:
            return
        self._motor_count = count
        idx = self._motor_count_combo.findData(count)
        if idx >= 0:
            self._motor_count_combo.blockSignals(True)
            self._motor_count_combo.setCurrentIndex(idx)
            self._motor_count_combo.blockSignals(False)
        if hasattr(self, "_angle_motor_count_combo"):
            idx_a = self._angle_motor_count_combo.findData(count)
            if idx_a >= 0:
                self._angle_motor_count_combo.blockSignals(True)
                self._angle_motor_count_combo.setCurrentIndex(idx_a)
                self._angle_motor_count_combo.blockSignals(False)
        if hasattr(self, "_distance_motor_count_combo"):
            idx_d = self._distance_motor_count_combo.findData(count)
            if idx_d >= 0:
                self._distance_motor_count_combo.blockSignals(True)
                self._distance_motor_count_combo.setCurrentIndex(idx_d)
                self._distance_motor_count_combo.blockSignals(False)
        self._apply_motor_count_ui()

    def set_rpm_subscribed(self, subscribed: bool) -> None:
        self._rpm_subscribed = subscribed
        if subscribed:
            self._lbl_rpm_hint.setText(
                "电机 RPM 已订阅 — 实线=实测，同色虚线=目标"
            )
            self._lbl_rpm_hint.setStyleSheet("color: #2d7a2d;")
        else:
            self._lbl_rpm_hint.setText(
                "未订阅电机 RPM：在仪表盘勾选「电机 RPM」并点「应用订阅」"
            )
            self._lbl_rpm_hint.setStyleSheet("color: #c8860a; font-weight: bold;")

    def _on_motor_count_combo(self) -> None:
        count = self._motor_count_combo.currentData()
        if count is None:
            return
        self._motor_count = int(count)
        self._apply_motor_count_ui()

    def _on_motor_visibility(self, motor_index: int, visible: bool) -> None:
        if 0 <= motor_index < len(self._motor_visible):
            self._motor_visible[motor_index] = visible
            if not self._charts_paused:
                self._redraw_rpm()

    def _apply_motor_count_ui(self) -> None:
        for i in range(proto.MOTOR_COUNT_MAX):
            active = i < self._motor_count
            was_disabled = not self._motor_checks[i].isEnabled()
            self._motor_checks[i].setEnabled(active)
            if not active:
                self._motor_checks[i].blockSignals(True)
                self._motor_checks[i].setChecked(False)
                self._motor_checks[i].blockSignals(False)
                self._motor_visible[i] = False
            else:
                if was_disabled:
                    self._motor_checks[i].setChecked(True)
                self._motor_visible[i] = self._motor_checks[i].isChecked()
            self._rpm_curves[i].setVisible(active and self._motor_visible[i])
        self._speed_panel.set_motor_count(self._motor_count)
        if hasattr(self, "_angle_panel"):
            self._angle_panel.set_motor_count(self._motor_count)
        self._lbl_enc_live.setText(self._format_motor_live_text([None] * proto.MOTOR_COUNT_MAX))
        self._lbl_rpm_live.setText(self._format_motor_live_text([None] * proto.MOTOR_COUNT_MAX))
        if not self._charts_paused:
            self._redraw_encoder()
            self._redraw_rpm()

    def _format_motor_live_text(
        self,
        values: list[Optional[int]],
        targets: Optional[list[int]] = None,
    ) -> str:
        parts: list[str] = []
        for i in range(self._motor_count):
            if values[i] is None:
                parts.append(f"{self.MOTOR_NAMES[i]}=—")
                continue
            text = f"{self.MOTOR_NAMES[i]}={values[i]}"
            if targets is not None and self._target_visible(i):
                tgt = targets[i]
                delta = int(values[i]) - int(tgt)
                text += f"→{tgt}(Δ{delta:+d})"
            parts.append(text)
        return "  ".join(parts)

    def _target_visible(self, motor_index: int) -> bool:
        if motor_index >= self._motor_count or not self._motor_visible[motor_index]:
            return False
        tgt = self._rpm_targets[motor_index]
        if tgt == 0:
            return False
        if self._rpm_target_mode == 0:
            return self._rpm_target_motor_id == motor_index + 1
        return True

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
        self.reset_angle_plot()
        self.reset_distance_plot()
        self._angle_target_yaw = 0
        self._lbl_enc_live.setText(self._format_motor_live_text([None] * proto.MOTOR_COUNT_MAX))
        self._lbl_rpm_live.setText(self._format_motor_live_text([None] * proto.MOTOR_COUNT_MAX))
        self.set_rpm_subscribed(False)
        self.set_angle_subscribed(False)
        self.set_distance_subscribed(False)
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
        self._rpm_targets = [0] * proto.MOTOR_COUNT_MAX
        self._last_rpms = [0] * proto.MOTOR_COUNT_MAX
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
            active = i < self._motor_count
            curve.setVisible(active)
            if active and n:
                curve.setData(xs, self._enc_data[i][-n:])
            else:
                curve.setData([], [])
        self._set_time_window(self._plot_enc, self._enc_time)

    def _redraw_rpm(self) -> None:
        n = min(len(self._rpm_time), *(len(s) for s in self._rpm_data)) if self._rpm_time else 0
        xs = self._rpm_time[-n:] if n else []
        y_vals: list[float] = []
        for i, curve in enumerate(self._rpm_curves):
            active = i < self._motor_count and self._motor_visible[i]
            curve.setVisible(active)
            tgt_curve = self._target_curves[i]
            show_target = self._target_visible(i)
            if active and n:
                ys = self._rpm_data[i][-n:]
                curve.setData(xs, ys)
                y_vals.extend(ys)
            else:
                curve.setData([], [])
            tgt_curve.setVisible(show_target)
            if show_target and xs:
                tgt = float(self._rpm_targets[i])
                tgt_curve.setData(xs, [tgt] * len(xs))
                y_vals.append(tgt)
            else:
                tgt_curve.setData([], [])
        self._set_time_window(self._plot_rpm, self._rpm_time)
        if y_vals:
            y_min = min(y_vals)
            y_max = max(y_vals)
            if y_min == y_max:
                pad = max(10.0, abs(y_min) * 0.1 + 5.0)
                self._plot_rpm.setYRange(y_min - pad, y_max + pad, padding=0.02)
            else:
                pad = max(5.0, (y_max - y_min) * 0.1)
                self._plot_rpm.setYRange(y_min - pad, y_max + pad, padding=0.02)
        self._lbl_rpm_live.setText(
            self._format_motor_live_text(self._last_rpms, self._rpm_targets)
        )

    def _redraw_all(self) -> None:
        self._redraw_attitude()
        self._redraw_line()
        self._redraw_encoder()
        self._redraw_rpm()
        if hasattr(self, "_plot_angle"):
            self._redraw_angle()
        if hasattr(self, "_plot_distance"):
            self._redraw_distance()

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
        self._angle_recorder.record_attitude(r, p, y)

        if not self._charts_paused:
            self._redraw_attitude()
        else:
            self._lbl_att_live.setText(
                f"roll={r:.0f}  pitch={p:.0f}  yaw={y:.0f}  (°)  [暂停]"
            )
        self.on_attitude_yaw_for_angle(y)

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
        self._lbl_enc_live.setText(self._format_motor_live_text(list(counts)))
        t = self._plot_time_s()
        self._enc_time.append(t)
        for i in range(proto.MOTOR_COUNT_MAX):
            self._enc_data[i].append(float(counts[i]))
        self._trim_series(self._enc_time, *self._enc_data)
        self._angle_recorder.record_encoder(counts)
        if not self._charts_paused:
            self._redraw_encoder()

    def on_motor_rpm(self, rpms: tuple[int, int, int, int]) -> None:
        self._last_rpms = list(rpms)
        t = self._plot_time_s()
        self._rpm_time.append(t)
        for i in range(proto.MOTOR_COUNT_MAX):
            self._rpm_data[i].append(float(rpms[i]))
        self._trim_series(self._rpm_time, *self._rpm_data)
        self._angle_recorder.record_motor_rpm(rpms)
        if not self._charts_paused:
            self._redraw_rpm()
        else:
            self._lbl_rpm_live.setText(
                self._format_motor_live_text(self._last_rpms, self._rpm_targets)
            )

    def _update_target_lines(self, target: list[int], mode: int, motor_id: int) -> None:
        self._rpm_targets = list(target[: proto.MOTOR_COUNT_MAX])
        while len(self._rpm_targets) < proto.MOTOR_COUNT_MAX:
            self._rpm_targets.append(0)
        self._rpm_target_mode = int(mode)
        self._rpm_target_motor_id = int(motor_id)
        if not self._charts_paused:
            self._redraw_rpm()
        else:
            self._lbl_rpm_live.setText(
                self._format_motor_live_text(self._last_rpms, self._rpm_targets)
            )
