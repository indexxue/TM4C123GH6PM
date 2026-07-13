"""PID 整定实验室：分场景预设、阶跃测试、响应曲线与调参向导。"""

from __future__ import annotations

import struct
import time
from dataclasses import dataclass
from typing import Optional

import pyqtgraph as pg
import tm_proto as proto
from PySide6.QtCore import Qt
from PySide6.QtWidgets import (
    QComboBox,
    QDoubleSpinBox,
    QFormLayout,
    QGroupBox,
    QHBoxLayout,
    QLabel,
    QPushButton,
    QSpinBox,
    QSplitter,
    QTextBrowser,
    QVBoxLayout,
    QWidget,
)

from serial_worker import SerialWorker


@dataclass(frozen=True)
class PidLoopProfile:
    key: str
    title: str
    param_id: int
    param_name: str
    tel_channel: Optional[int]
    cap_flag: Optional[int]
    default_kp: float
    default_ki: float
    default_kd: float
    guide: str


@dataclass(frozen=True)
class PidScenario:
    key: str
    title: str
    loop_key: str
    kp: float
    ki: float
    kd: float
    hint: str
    # 阶跃测试参数（按环解释）
    step_a: int
    step_b: int
    step_c: int


LOOP_PROFILES: list[PidLoopProfile] = [
    PidLoopProfile(
        key="speed",
        title="速度环 (pid_speed)",
        param_id=5,
        param_name="pid_speed",
        tel_channel=int(proto.TelChannel.MOTOR_RPM),
        cap_flag=int(proto.Cap.SPEED_LOOP),
        default_kp=1.0,
        default_ki=0.2,
        default_kd=0.0,
        guide=(
            "速度环直接跟踪轮速 RPM，内环最基础。\n"
            "1. Ki=Kd=0，从小 Kp 起步（如 0.3），阶跃 80 RPM，逐步加 Kp 至轻微振荡再回退 20%。\n"
            "2. 加 Kd（0.05~0.2）抑制超调。\n"
            "3. 最后加 Ki 消除静差（0.05~0.3），注意积分饱和。\n"
            "现象：振荡→降 Kp 或加 Kd；静差→加 Ki；响应慢→加 Kp。"
        ),
    ),
    PidLoopProfile(
        key="angle",
        title="角度环 (pid_yaw)",
        param_id=16,
        param_name="pid_yaw",
        tel_channel=int(proto.TelChannel.ANGLE_LOOP),
        cap_flag=int(proto.Cap.ANGLE_LOOP),
        default_kp=2.0,
        default_ki=0.0,
        default_kd=0.5,
        guide=(
            "角度环输出左右差速 RPM，反馈为编码器里程计 Δθ。\n"
            "1. 先 Ki=0，小角度阶跃（±30°）调 Kp，原地转（base_rpm=0）更易观察。\n"
            "2. Kd 用陀螺阻尼，固件对 kd 有专门处理；一般 0.3~0.8。\n"
            "3. Ki 仅在持续静差时少量添加。\n"
            "现象：转向过头→降 Kp 或加 Kd；不到位→加 Kp；左右不对称先查电机/编码器极性。"
        ),
    ),
    PidLoopProfile(
        key="distance",
        title="距离环 (pid_dist)",
        param_id=18,
        param_name="pid_dist",
        tel_channel=int(proto.TelChannel.DISTANCE_LOOP),
        cap_flag=int(proto.Cap.DISTANCE_LOOP),
        default_kp=0.8,
        default_ki=0.05,
        default_kd=0.02,
        guide=(
            "距离环以编码器积分位移（mm）为反馈，输出同速 RPM。\n"
            "1. 短距（200~500 mm）先调 Kp（0.3~1.2），Ki 先保持 0。\n"
            "2. 加小 Ki 消静差；Kd 抑制到位振荡（0~0.05）。\n"
            "3. max_rpm 限低（40~80）更安全，避免冲击电流。\n"
            "现象：冲过头→降 Kp；爬不到目标→加 Kp 或略加 Ki；到位抖动→加 Kd 或减 Kp。"
        ),
    ),
    PidLoopProfile(
        key="line",
        title="循迹环 (pid_line)",
        param_id=6,
        param_name="pid_line",
        tel_channel=None,
        cap_flag=None,
        default_kp=2.0,
        default_ki=0.0,
        default_kd=0.1,
        guide=(
            "循迹环根据横向偏差修正转向，需实车沿黑线观察。\n"
            "本页可读写 pid_line；自动阶跃测试需手推小车或后续接循迹偏差推送。\n"
            "建议：基准速度先低（80 RPM 级），Kp 从小到大，Kd 抑制 S 弯摆动。"
        ),
    ),
]

SCENARIOS: list[PidScenario] = [
    PidScenario("spd_low", "速度·低速阶跃", "speed", 0.5, 0.1, 0.0,
                "M1 目标 80 RPM", 1, 80, 0),
    PidScenario("spd_mid", "速度·中速阶跃", "speed", 1.0, 0.2, 0.0,
                "M1 目标 150 RPM", 1, 150, 0),
    PidScenario("spd_high", "速度·高速阶跃", "speed", 1.2, 0.25, 0.05,
                "M1 目标 220 RPM", 1, 220, 0),
    PidScenario("spd_lr", "速度·左右同速", "speed", 1.0, 0.2, 0.0,
                "左右 100 RPM（format=1）", 0, 100, 100),
    PidScenario("ang_small", "角度·小转角", "angle", 2.0, 0.0, 0.5,
                "Δθ=+30° 原地", 30, 0, 0),
    PidScenario("ang_large", "角度·大转角", "angle", 2.5, 0.0, 0.6,
                "Δθ=+90° 原地", 90, 0, 0),
    PidScenario("ang_move", "角度·边转边走", "angle", 2.0, 0.0, 0.4,
                "Δθ=45° base=60 RPM", 45, 60, 0),
    PidScenario("dist_short", "距离·短距", "distance", 0.6, 0.03, 0.01,
                "前进 300 mm @60 RPM", 300, 60, 0),
    PidScenario("dist_mid", "距离·中距", "distance", 0.8, 0.05, 0.02,
                "前进 500 mm @80 RPM", 500, 80, 0),
    PidScenario("dist_back", "距离·后退", "distance", 0.8, 0.05, 0.02,
                "后退 -400 mm @60 RPM", -400, 60, 0),
    PidScenario("line_gentle", "循迹·保守", "line", 1.2, 0.0, 0.08,
                "仅加载 PID，请实车循迹", 0, 0, 0),
    PidScenario("line_aggr", "循迹·激进", "line", 2.5, 0.05, 0.15,
                "仅加载 PID，请实车循迹", 0, 0, 0),
]

LOOP_BY_KEY = {p.key: p for p in LOOP_PROFILES}


def _compute_metrics(target: float, series: list[float]) -> dict[str, Optional[float]]:
    if not series:
        return {"steady_err": None, "overshoot_pct": None, "settle_s": None}
    final_target = target
    if abs(final_target) < 1e-6:
        steady_err = float(series[-1])
        overshoot = None
    else:
        steady_err = final_target - float(series[-1])
        peak = max(series) if final_target > 0 else min(series)
        if final_target > 0:
            overshoot = max(0.0, (peak - final_target) / final_target * 100.0)
        else:
            overshoot = max(0.0, (final_target - peak) / abs(final_target) * 100.0)
    settle_s = None
    if abs(final_target) >= 1e-6:
        band = max(5.0, abs(final_target) * 0.05)
        for i in range(len(series) - 1, -1, -1):
            if abs(series[i] - final_target) > band:
                settle_s = (i + 1) * 0.05
                break
        else:
            settle_s = 0.0
    return {
        "steady_err": steady_err,
        "overshoot_pct": overshoot,
        "settle_s": settle_s,
    }


class PidTuningTab(QWidget):
    HISTORY = 800
    TIME_WINDOW_S = 45.0

    def __init__(self, worker: SerialWorker, parent: Optional[QWidget] = None):
        super().__init__(parent)
        self._worker = worker
        self._caps = 0
        self._motor_count = proto.MOTOR_COUNT_DEFAULT
        self._recording = False
        self._time_origin: Optional[float] = None
        self._step_target = 0.0
        self._times: list[float] = []
        self._feedback: list[float] = []
        self._target_series: list[float] = []

        root = QVBoxLayout(self)
        root.addLayout(self._build_toolbar())

        splitter = QSplitter(Qt.Horizontal)
        splitter.addWidget(self._build_left_panel())
        splitter.addWidget(self._build_chart_panel())
        splitter.setStretchFactor(0, 2)
        splitter.setStretchFactor(1, 3)
        root.addWidget(splitter, stretch=1)

        self._on_loop_changed()
        self._refresh_scenario_combo()

    def _build_toolbar(self) -> QHBoxLayout:
        row = QHBoxLayout()
        row.addWidget(QLabel("控制环"))
        self._loop_combo = QComboBox()
        for p in LOOP_PROFILES:
            self._loop_combo.addItem(p.title, p.key)
        self._loop_combo.currentIndexChanged.connect(self._on_loop_changed)
        row.addWidget(self._loop_combo, stretch=1)

        row.addWidget(QLabel("场景"))
        self._scenario_combo = QComboBox()
        self._scenario_combo.currentIndexChanged.connect(self._on_scenario_changed)
        row.addWidget(self._scenario_combo, stretch=1)

        row.addWidget(QLabel("车型"))
        self._motor_combo = QComboBox()
        self._motor_combo.addItem("两轮 (M1~M2)", proto.MOTOR_COUNT_DEFAULT)
        self._motor_combo.addItem("四轮 (M1~M4)", proto.MOTOR_COUNT_MAX)
        self._motor_combo.currentIndexChanged.connect(self._on_motor_combo)
        row.addWidget(self._motor_combo)
        return row

    def _build_left_panel(self) -> QWidget:
        page = QWidget()
        layout = QVBoxLayout(page)

        pid_box = QGroupBox("PID 系数")
        pid_form = QFormLayout(pid_box)
        self._kp = self._make_gain_spin(0.0, 50.0, 0.05)
        self._ki = self._make_gain_spin(0.0, 20.0, 0.01)
        self._kd = self._make_gain_spin(0.0, 10.0, 0.01)
        pid_form.addRow("Kp", self._kp)
        pid_form.addRow("Ki", self._ki)
        pid_form.addRow("Kd", self._kd)
        btn_row = QHBoxLayout()
        self._btn_read = QPushButton("从设备读取")
        self._btn_write = QPushButton("写入设备")
        self._btn_preset = QPushButton("套用场景 PID")
        btn_row.addWidget(self._btn_read)
        btn_row.addWidget(self._btn_write)
        btn_row.addWidget(self._btn_preset)
        pid_form.addRow(btn_row)
        layout.addWidget(pid_box)

        step_box = QGroupBox("阶跃参数")
        step_form = QFormLayout(step_box)
        self._lbl_step_hint = QLabel("—")
        self._lbl_step_hint.setWordWrap(True)
        self._lbl_step_hint.setStyleSheet("color: palette(mid);")
        step_form.addRow(self._lbl_step_hint)

        self._step_a = QSpinBox()
        self._step_a.setRange(-10000, 10000)
        self._step_b = QSpinBox()
        self._step_b.setRange(-2000, 2000)
        self._step_c = QSpinBox()
        self._step_c.setRange(0, 500)
        self._row_a = QLabel("参数 A")
        self._row_b = QLabel("参数 B")
        self._row_c = QLabel("参数 C")
        step_form.addRow(self._row_a, self._step_a)
        step_form.addRow(self._row_b, self._step_b)
        step_form.addRow(self._row_c, self._step_c)
        layout.addWidget(step_box)

        run_row = QHBoxLayout()
        self._btn_start = QPushButton("开始阶跃测试")
        self._btn_stop = QPushButton("停止")
        self._btn_clear = QPushButton("清空曲线")
        run_row.addWidget(self._btn_start)
        run_row.addWidget(self._btn_stop)
        run_row.addWidget(self._btn_clear)
        layout.addLayout(run_row)

        self._lbl_status = QLabel("就绪：选择场景后「套用 PID」→「开始阶跃测试」")
        self._lbl_status.setWordWrap(True)
        layout.addWidget(self._lbl_status)

        self._lbl_metrics = QLabel("指标：—")
        layout.addWidget(self._lbl_metrics)

        guide_box = QGroupBox("调参向导")
        guide_layout = QVBoxLayout(guide_box)
        self._guide = QTextBrowser()
        self._guide.setOpenExternalLinks(False)
        guide_layout.addWidget(self._guide)
        layout.addWidget(guide_box, stretch=1)

        self._btn_read.clicked.connect(self._read_pid)
        self._btn_write.clicked.connect(self._write_pid)
        self._btn_preset.clicked.connect(self._apply_scenario_pid)
        self._btn_start.clicked.connect(self._start_step)
        self._btn_stop.clicked.connect(self._stop_step)
        self._btn_clear.clicked.connect(self.clear_plot)
        return page

    def _build_chart_panel(self) -> QWidget:
        page = QWidget()
        layout = QVBoxLayout(page)
        self._plot = pg.PlotWidget(title="阶跃响应")
        self._plot.showGrid(x=True, y=True, alpha=0.3)
        self._plot.setLabel("bottom", "时间", units="s")
        self._plot.setLabel("left", "反馈")
        legend = self._plot.addLegend(offset=(10, 10))
        self._curve_fb = self._plot.plot(pen=pg.mkPen("#3498db", width=2))
        self._curve_tgt = self._plot.plot(
            pen=pg.mkPen("#e67e22", width=2, style=Qt.PenStyle.DashLine),
        )
        legend.addItem(self._curve_fb, "反馈")
        legend.addItem(self._curve_tgt, "目标")
        layout.addWidget(self._plot, stretch=1)
        self._lbl_plot_hint = QLabel("测试时自动订阅对应遥测通道（RPM / 角度环 / 距离环）")
        self._lbl_plot_hint.setStyleSheet("color: #666;")
        layout.addWidget(self._lbl_plot_hint)
        return page

    @staticmethod
    def _make_gain_spin(lo: float, hi: float, step: float) -> QDoubleSpinBox:
        sp = QDoubleSpinBox()
        sp.setRange(lo, hi)
        sp.setDecimals(4)
        sp.setSingleStep(step)
        sp.setKeyboardTracking(False)
        return sp

    def _current_loop(self) -> PidLoopProfile:
        key = self._loop_combo.currentData()
        return LOOP_BY_KEY[str(key)]

    def _current_scenario(self) -> Optional[PidScenario]:
        data = self._scenario_combo.currentData()
        if data is None:
            return None
        for s in SCENARIOS:
            if s.key == data:
                return s
        return None

    def _on_loop_changed(self) -> None:
        loop = self._current_loop()
        self._kp.setValue(loop.default_kp)
        self._ki.setValue(loop.default_ki)
        self._kd.setValue(loop.default_kd)
        self._guide.setPlainText(loop.guide)
        self._refresh_scenario_combo()
        self._update_step_labels()
        self._plot.setLabel("left", self._ylabel_for_loop(loop.key))

    def _refresh_scenario_combo(self) -> None:
        loop_key = self._loop_combo.currentData()
        self._scenario_combo.blockSignals(True)
        self._scenario_combo.clear()
        for s in SCENARIOS:
            if s.loop_key == loop_key:
                self._scenario_combo.addItem(s.title, s.key)
        self._scenario_combo.blockSignals(False)
        if self._scenario_combo.count() > 0:
            self._on_scenario_changed()

    def _on_scenario_changed(self) -> None:
        sc = self._current_scenario()
        if sc is None:
            return
        self._kp.setValue(sc.kp)
        self._ki.setValue(sc.ki)
        self._kd.setValue(sc.kd)
        self._step_a.setValue(sc.step_a)
        self._step_b.setValue(sc.step_b)
        self._step_c.setValue(sc.step_c)
        self._lbl_step_hint.setText(sc.hint)
        self._update_step_labels()

    def _on_motor_combo(self) -> None:
        data = self._motor_combo.currentData()
        if data is not None:
            self._motor_count = int(data)

    def _update_step_labels(self) -> None:
        key = self._loop_combo.currentData()
        if key == "speed":
            self._row_a.setText("电机 ID (单轮)")
            self._row_b.setText("目标 RPM / 左 RPM")
            self._row_c.setText("右 RPM (左右模式)")
            self._step_a.setRange(1, 4)
            self._step_b.setRange(-500, 500)
            self._step_c.setRange(-500, 500)
            self._row_c.setVisible(True)
            self._step_c.setVisible(True)
        elif key == "angle":
            self._row_a.setText("Δθ (°)")
            self._row_b.setText("base RPM")
            self._row_c.setText("max_turn (0=默认)")
            self._step_a.setRange(-180, 180)
            self._step_b.setRange(-300, 300)
            self._step_c.setRange(0, 500)
            self._row_c.setVisible(True)
            self._step_c.setVisible(True)
        elif key == "distance":
            self._row_a.setText("距离 (mm)")
            self._row_b.setText("max RPM")
            self._row_c.setVisible(False)
            self._step_c.setVisible(False)
            self._step_a.setRange(-10000, 10000)
            self._step_b.setRange(0, 500)
        else:
            self._row_a.setText("—")
            self._row_b.setText("—")
            self._row_c.setVisible(False)
            self._step_c.setVisible(False)

    @staticmethod
    def _ylabel_for_loop(loop_key: str) -> str:
        if loop_key == "speed":
            return "RPM"
        if loop_key == "angle":
            return "Δθ (°)"
        if loop_key == "distance":
            return "位移 (mm)"
        return "反馈"

    def on_hello(self, caps: int) -> None:
        self._caps = caps

    def set_motor_count(self, count: int) -> None:
        count = max(2, min(proto.MOTOR_COUNT_MAX, int(count)))
        self._motor_count = count
        idx = self._motor_combo.findData(count)
        if idx >= 0:
            self._motor_combo.blockSignals(True)
            self._motor_combo.setCurrentIndex(idx)
            self._motor_combo.blockSignals(False)

    def apply_param_read(self, param_id: int, payload: bytes) -> None:
        loop = self._current_loop()
        if param_id != loop.param_id or len(payload) < 12:
            return
        kp, ki, kd = struct.unpack_from("<fff", payload, 0)
        self._kp.setValue(kp)
        self._ki.setValue(ki)
        self._kd.setValue(kd)
        self._set_status(f"已载入 {loop.param_name}: Kp={kp:.4g} Ki={ki:.4g} Kd={kd:.4g}")

    def on_motor_rpm(self, rpms: tuple[int, int, int, int]) -> None:
        if not self._recording or self._current_loop().key != "speed":
            return
        loop = self._current_loop()
        sc = self._current_scenario()
        if sc and sc.key == "spd_lr":
            fb = (rpms[0] + rpms[1]) / 2 if self._motor_count == 2 else (rpms[0] + rpms[2]) / 2
        else:
            mid = max(0, min(3, self._step_a.value() - 1))
            fb = float(rpms[mid])
        self._append_sample(fb, self._step_target)

    def on_angle_loop(self, sample: proto.AngleLoopPush) -> None:
        if not self._recording or self._current_loop().key != "angle":
            return
        self._append_sample(float(sample.current_yaw), float(sample.target_yaw))

    def on_distance_loop(self, sample: proto.DistanceLoopPush) -> None:
        if not self._recording or self._current_loop().key != "distance":
            return
        self._append_sample(float(sample.current_mm), float(sample.target_mm))

    def reset(self) -> None:
        self._caps = 0
        self._recording = False
        self.clear_plot()

    def clear_plot(self) -> None:
        self._time_origin = None
        self._times.clear()
        self._feedback.clear()
        self._target_series.clear()
        self._curve_fb.setData([], [])
        self._curve_tgt.setData([], [])
        self._lbl_metrics.setText("指标：—")

    def _read_pid(self) -> None:
        loop = self._current_loop()
        self._worker.request_param_read(loop.param_id)
        self._set_status(f"已请求读取 {loop.param_name}")

    def _write_pid(self) -> None:
        loop = self._current_loop()
        payload = struct.pack("<fff", self._kp.value(), self._ki.value(), self._kd.value())
        self._worker.request_param_write(loop.param_id, payload)
        self._set_status(f"已请求写入 {loop.param_name}")

    def _apply_scenario_pid(self) -> None:
        sc = self._current_scenario()
        if sc is None:
            return
        self._kp.setValue(sc.kp)
        self._ki.setValue(sc.ki)
        self._kd.setValue(sc.kd)
        self._set_status(f"已套用场景「{sc.title}」PID（未写入设备）")

    def _ensure_telemetry(self, loop: PidLoopProfile) -> bool:
        if loop.tel_channel is None:
            return True
        if loop.cap_flag and not (self._caps & loop.cap_flag):
            self._set_status(f"固件不支持 {loop.title}")
            return False
        mask = getattr(self._worker, "optional_mask", 0) | loop.tel_channel
        self._worker.request_apply_subscription(mask)
        return True

    def _start_step(self) -> None:
        loop = self._current_loop()
        if loop.key == "line":
            self._write_pid()
            self._set_status("循迹环请实车验证；已请求写入 pid_line")
            return
        if not self._ensure_telemetry(loop):
            return

        self.clear_plot()
        self._recording = True
        self._write_pid()

        if loop.key == "speed":
            self._start_speed_step()
        elif loop.key == "angle":
            self._start_angle_step()
        elif loop.key == "distance":
            self._start_distance_step()

    def _start_speed_step(self) -> None:
        sc = self._current_scenario()
        if sc and sc.key == "spd_lr":
            left = self._step_b.value()
            right = self._step_c.value() if self._step_c.value() != 0 else left
            self._step_target = float((left + right) / 2)
            self._worker.request_set_speed(proto.build_set_speed_lr(left, right))
            self._set_status(f"阶跃：左右 RPM L={left} R={right}")
            return
        mid = self._step_a.value()
        rpm = self._step_b.value()
        self._step_target = float(rpm)
        if self._motor_count == 2 and mid > 2:
            mid = 1
        self._worker.request_set_speed(proto.build_set_speed_wheel(mid, rpm))
        self._set_status(f"阶跃：M{mid} → {rpm} RPM")

    def _start_angle_step(self) -> None:
        delta = self._step_a.value()
        base = self._step_b.value()
        max_turn = self._step_c.value()
        self._step_target = float(delta)
        self._worker.request_set_angle(proto.build_set_angle(delta, base, max_turn))
        self._set_status(f"阶跃：Δθ={delta:+d}° base={base} RPM")

    def _start_distance_step(self) -> None:
        dist = self._step_a.value()
        max_rpm = self._step_b.value()
        self._step_target = float(dist)
        self._worker.request_set_distance(proto.build_set_distance(dist, max_rpm))
        self._set_status(f"阶跃：Δs={dist:+d} mm max_rpm={max_rpm}")

    def _stop_step(self) -> None:
        self._recording = False
        loop = self._current_loop()
        if loop.key == "speed":
            self._worker.request_speed_stop()
        elif loop.key == "angle":
            self._worker.request_angle_stop()
        elif loop.key == "distance":
            self._worker.request_distance_stop()
        self._update_metrics()
        self._set_status("已停止并计算指标")

    def _append_sample(self, feedback: float, target: float) -> None:
        t = self._now_s()
        self._times.append(t)
        self._feedback.append(feedback)
        self._target_series.append(target)
        while len(self._times) > self.HISTORY:
            self._times.pop(0)
            self._feedback.pop(0)
            self._target_series.pop(0)
        self._curve_fb.setData(self._times, self._feedback)
        self._curve_tgt.setData(self._times, self._target_series)
        if self._times:
            t_max = self._times[-1]
            self._plot.setXRange(max(0.0, t_max - self.TIME_WINDOW_S), t_max, padding=0.02)

    def _now_s(self) -> float:
        if self._time_origin is None:
            self._time_origin = time.monotonic()
        return time.monotonic() - self._time_origin

    def _update_metrics(self) -> None:
        tgt = self._step_target
        if self._feedback and self._current_loop().key == "angle":
            tgt = self._target_series[-1] if self._target_series else tgt
        m = _compute_metrics(tgt, self._feedback)
        parts = []
        if m["steady_err"] is not None:
            parts.append(f"稳态误差={m['steady_err']:.2f}")
        if m["overshoot_pct"] is not None:
            parts.append(f"超调≈{m['overshoot_pct']:.1f}%")
        if m["settle_s"] is not None:
            parts.append(f"调节≈{m['settle_s']:.2f}s")
        self._lbl_metrics.setText("指标：" + ("  ".join(parts) if parts else "—"))

    def _set_status(self, text: str) -> None:
        self._lbl_status.setText(text)
