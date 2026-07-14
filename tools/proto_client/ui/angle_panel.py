"""航向角环控制面板（SET_ANGLE / pid_yaw 快捷读 / 自动测试序列）。"""

from __future__ import annotations

import json
import threading
from pathlib import Path
from typing import Callable, Optional

import tm_proto as proto
from PySide6.QtCore import Qt, Signal, Slot
from PySide6.QtWidgets import (
    QCheckBox,
    QComboBox,
    QFormLayout,
    QGroupBox,
    QHBoxLayout,
    QLabel,
    QMessageBox,
    QPushButton,
    QSpinBox,
    QVBoxLayout,
    QWidget,
)

from angle_test_runner import (
    AngleTestRunner,
    DEFAULT_TEST_ANGLES,
    QUICK_TEST_ANGLES,
    TestSequenceConfig,
    TestSequenceResult,
)
from serial_worker import SerialWorker
from ui.param_panel import ParamEditor


def normalize_yaw(deg: int) -> int:
    """归一化到 (-180, 180]。"""
    d = int(deg) % 360
    if d > 180:
        d -= 360
    if d <= -180:
        d += 360
    return d


class AngleControlPanel(QGroupBox):
    DEFAULT_DELTA = 30
    YAW_STEP = 30
    DEFAULT_BASE_RPM = 0

    # 测试进度信号
    test_log = Signal(str)
    test_finished = Signal(object)

    def __init__(
        self,
        worker: SerialWorker,
        pid_yaw_editor: Optional[ParamEditor],
        spd_limit_editor: Optional[ParamEditor],
        on_clear_plot: Optional[Callable[[], None]] = None,
        on_log_start: Optional[Callable[[], str]] = None,
        on_log_export: Optional[Callable[[], str]] = None,
        on_log_command: Optional[Callable[..., None]] = None,
        on_get_recorder: Optional[Callable[[], object]] = None,
        on_get_pid_params: Optional[Callable[[], dict]] = None,
        parent: Optional[QWidget] = None,
    ):
        super().__init__("角度环调试", parent)
        self._worker = worker
        self._pid_yaw_editor = pid_yaw_editor
        self._spd_limit_editor = spd_limit_editor
        self._on_clear_plot = on_clear_plot
        self._on_log_start = on_log_start
        self._on_log_export = on_log_export
        self._on_log_command = on_log_command
        self._on_get_recorder = on_get_recorder
        self._on_get_pid_params = on_get_pid_params
        self._delta_yaw = self.DEFAULT_DELTA
        self._base_rpm = self.DEFAULT_BASE_RPM
        self._motor_count = proto.MOTOR_COUNT_DEFAULT
        self._test_runner: Optional[AngleTestRunner] = None
        self._test_running = False

        root = QVBoxLayout(self)

        pid_row = QHBoxLayout()
        self._btn_read_pid = QPushButton("读取 pid_yaw")
        self._btn_read_lim = QPushButton("读取 spd_limit")
        pid_row.addWidget(self._btn_read_pid)
        pid_row.addWidget(self._btn_read_lim)
        pid_row.addStretch()
        root.addLayout(pid_row)

        cmd_form = QFormLayout()

        yaw_row = QWidget()
        yaw_layout = QHBoxLayout(yaw_row)
        yaw_layout.setContentsMargins(0, 0, 0, 0)
        self._btn_yaw_dec = QPushButton(f"−{self.YAW_STEP}°")
        self._btn_yaw_inc = QPushButton(f"+{self.YAW_STEP}°")
        self._delta_yaw_spin = QSpinBox()
        self._delta_yaw_spin.setRange(-180, 180)
        self._delta_yaw_spin.setSingleStep(self.YAW_STEP)
        self._delta_yaw_spin.setWrapping(True)
        self._delta_yaw_spin.setValue(self.DEFAULT_DELTA)
        self._delta_yaw_spin.setSuffix(" °")
        yaw_layout.addWidget(self._btn_yaw_dec)
        yaw_layout.addWidget(self._delta_yaw_spin, stretch=1)
        yaw_layout.addWidget(self._btn_yaw_inc)
        cmd_form.addRow("相对转角 Δθ", yaw_row)

        self._base_rpm = self._make_rpm_spin(self.DEFAULT_BASE_RPM)
        cmd_form.addRow("基准 RPM", self._base_rpm)

        self._max_turn = QSpinBox()
        self._max_turn.setRange(0, 500)
        self._max_turn.setSingleStep(10)
        self._max_turn.setValue(0)
        self._max_turn.setSpecialValueText("默认 (max_rpm×0.5)")
        cmd_form.addRow("差速上限", self._max_turn)

        self._lr_hint = QLabel()
        self._lr_hint.setWordWrap(True)
        self._lr_hint.setStyleSheet("color: palette(mid);")
        cmd_form.addRow(self._lr_hint)

        btn_row = QHBoxLayout()
        self._btn_apply = QPushButton("下发 SET_ANGLE")
        self._btn_stop = QPushButton("ANGLE_STOP")
        self._btn_step = QPushButton(f"阶跃 +{self.DEFAULT_DELTA}°")
        self._btn_clear = QPushButton("清空角度曲线")
        btn_row.addWidget(self._btn_apply)
        btn_row.addWidget(self._btn_stop)
        btn_row.addWidget(self._btn_step)
        btn_row.addWidget(self._btn_clear)
        cmd_form.addRow(btn_row)

        calib_row = QWidget()
        calib_layout = QHBoxLayout(calib_row)
        calib_layout.setContentsMargins(0, 0, 0, 0)
        self._calib_yaw_spin = QSpinBox()
        self._calib_yaw_spin.setRange(-180, 180)
        self._calib_yaw_spin.setSingleStep(15)
        self._calib_yaw_spin.setWrapping(True)
        self._calib_yaw_spin.setValue(0)
        self._calib_yaw_spin.setSuffix(" °")
        self._calib_yaw_spin.setToolTip("校准后当前车头物理朝向将显示为该角度")
        self._btn_calib_yaw = QPushButton("校准物理 Yaw")
        calib_layout.addWidget(QLabel("参考角"))
        calib_layout.addWidget(self._calib_yaw_spin, stretch=1)
        calib_layout.addWidget(self._btn_calib_yaw)
        cmd_form.addRow("磁力计校准", calib_row)

        calib_hint = QLabel(
            "车辆须静止放平；将当前车头朝向设为上方参考角（默认 0°=正前方）。"
        )
        calib_hint.setWordWrap(True)
        calib_hint.setStyleSheet("color: palette(mid); font-size: 11px;")
        cmd_form.addRow(calib_hint)

        root.addLayout(cmd_form)

        log_box = QGroupBox("数据导出 (log/)")
        log_layout = QVBoxLayout(log_box)
        self._chk_auto_record = QCheckBox("下发 SET_ANGLE 时自动开始记录")
        self._chk_auto_record.setChecked(True)
        self._chk_auto_export = QCheckBox("ANGLE_STOP 时自动停止并导出")
        self._chk_auto_export.setChecked(True)
        log_layout.addWidget(self._chk_auto_record)
        log_layout.addWidget(self._chk_auto_export)
        log_btn_row = QHBoxLayout()
        self._btn_log_start = QPushButton("开始记录")
        self._btn_log_export = QPushButton("停止并导出")
        log_btn_row.addWidget(self._btn_log_start)
        log_btn_row.addWidget(self._btn_log_export)
        log_layout.addLayout(log_btn_row)
        self._log_hint = QLabel(
            "导出目录：工具当前目录下的 log 文件夹。"
            "建议勾选「角度环」「电机 RPM」订阅后再测试。"
        )
        self._log_hint.setWordWrap(True)
        self._log_hint.setStyleSheet("color: palette(mid); font-size: 11px;")
        log_layout.addWidget(self._log_hint)
        root.addWidget(log_box)

        # ---- 自动测试序列 ----
        self._test_box = QGroupBox("自动测试序列 (角度环 PID 诊断)")
        test_layout = QVBoxLayout(self._test_box)

        test_mode_row = QHBoxLayout()
        test_mode_row.addWidget(QLabel("测试模式"))
        self._test_mode_combo = QComboBox()
        self._test_mode_combo.addItem("快速验证 (3 个角度)", "quick")
        self._test_mode_combo.addItem("完整诊断 (8 个角度)", "full")
        test_mode_row.addWidget(self._test_mode_combo, stretch=1)
        test_layout.addLayout(test_mode_row)

        test_btn_row = QHBoxLayout()
        self._btn_run_test = QPushButton("▶ 运行自动测试")
        self._btn_run_test.setStyleSheet(
            "QPushButton { font-weight: bold; background: #2980b9; color: white; "
            "padding: 6px 16px; border-radius: 4px; }"
            "QPushButton:hover { background: #3498db; }"
            "QPushButton:disabled { background: #555; color: #888; }"
        )
        self._btn_abort_test = QPushButton("中断测试")
        self._btn_abort_test.setEnabled(False)
        test_btn_row.addWidget(self._btn_run_test, stretch=2)
        test_btn_row.addWidget(self._btn_abort_test, stretch=1)
        test_layout.addLayout(test_btn_row)

        self._test_progress = QLabel("就绪 — 点击按钮开始自动测试序列")
        self._test_progress.setWordWrap(True)
        self._test_progress.setStyleSheet("color: palette(mid); font-size: 11px;")
        test_layout.addWidget(self._test_progress)
        root.addWidget(self._test_box)

        self._status = QLabel("")
        root.addWidget(self._status)

        self._btn_read_pid.clicked.connect(self._read_pid)
        self._btn_read_lim.clicked.connect(self._read_limit)
        self._btn_apply.clicked.connect(self._apply_angle)
        self._btn_stop.clicked.connect(self._stop_angle)
        self._btn_step.clicked.connect(self._step_default_yaw)
        self._btn_clear.clicked.connect(self._clear_plot)
        self._btn_calib_yaw.clicked.connect(self._calib_yaw)
        self._btn_yaw_dec.clicked.connect(self._dec_yaw)
        self._btn_yaw_inc.clicked.connect(self._inc_yaw)
        self._delta_yaw_spin.valueChanged.connect(self._on_delta_spin_changed)
        self._btn_log_start.clicked.connect(lambda: self._start_log(silent=False))
        self._btn_log_export.clicked.connect(lambda: self._export_log(silent=False))
        self._btn_run_test.clicked.connect(self._run_test_sequence)
        self._btn_abort_test.clicked.connect(self._abort_test)
        self.test_log.connect(self._set_status)
        self.test_finished.connect(self._on_test_finished)
        worker.hello_received.connect(self._update_calib_available)

        self.set_motor_count(self._motor_count)
        self._refresh_log_hint()
        self._update_calib_available(worker.hello_info)

    @staticmethod
    def _make_rpm_spin(default: int) -> QSpinBox:
        sp = QSpinBox()
        sp.setRange(-500, 500)
        sp.setSingleStep(50)
        sp.setValue(default)
        return sp

    def set_motor_count(self, count: int) -> None:
        count = max(2, min(proto.MOTOR_COUNT_MAX, int(count)))
        self._motor_count = count
        if count == 2:
            self._lr_hint.setText(
                "SET_ANGLE 为相对转角 Δθ（在当前航向基础上再转 N°）。"
                "两轮：基准 RPM=0 为原地转向。"
            )
        else:
            self._lr_hint.setText(
                "SET_ANGLE 为相对转角 Δθ（在当前航向基础上再转 N°）。"
                "四轮：基准 RPM=0 为原地转向。"
            )

    @property
    def delta_yaw(self) -> int:
        return self._delta_yaw

    def set_target_line_callback(self, callback) -> None:
        self._target_line_callback = callback

    def _on_delta_spin_changed(self, value: int) -> None:
        self._delta_yaw = normalize_yaw(value)
        if self._delta_yaw != value:
            self._delta_yaw_spin.blockSignals(True)
            self._delta_yaw_spin.setValue(self._delta_yaw)
            self._delta_yaw_spin.blockSignals(False)

    def _dec_yaw(self) -> None:
        self._delta_yaw_spin.setValue(self._delta_yaw_spin.value() - self.YAW_STEP)

    def _inc_yaw(self) -> None:
        self._delta_yaw_spin.setValue(self._delta_yaw_spin.value() + self.YAW_STEP)

    def _read_pid(self) -> None:
        if self._pid_yaw_editor is not None:
            self._pid_yaw_editor.read()
            self._set_status("已请求读取 pid_yaw")

    def _read_limit(self) -> None:
        if self._spd_limit_editor is not None:
            self._spd_limit_editor.read()
            self._set_status("已请求读取 spd_limit")

    def _apply_angle(self) -> None:
        delta = normalize_yaw(self._delta_yaw_spin.value())
        base = self._base_rpm.value()
        max_turn = self._max_turn.value()
        self._delta_yaw = delta
        if self._chk_auto_record.isChecked():
            self._start_log(silent=True)
        self._emit_log_command("set_angle", target=delta, base=base, max_turn=max_turn)
        payload = proto.build_set_angle(delta, base, max_turn)
        self._worker.request_set_angle(payload)
        self._set_status(f"SET_ANGLE 已发送 Δθ={delta:+d}° base={base} RPM")

    def _stop_angle(self) -> None:
        self._emit_log_command("angle_stop")
        self._worker.request_angle_stop()
        if self._chk_auto_export.isChecked():
            self._export_log(silent=False, from_stop=True)
        else:
            self._set_status("ANGLE_STOP 已发送")

    def _emit_log_command(
        self,
        name: str,
        *,
        target: Optional[int] = None,
        base: Optional[int] = None,
        max_turn: Optional[int] = None,
        note: str = "",
    ) -> None:
        if self._on_log_command is None:
            return
        self._on_log_command(
            name,
            target=target,
            base=base,
            max_turn=max_turn,
            note=note,
        )

    def _start_log(self, silent: bool = False) -> None:
        if self._on_log_start is None:
            self._set_status("记录功能未就绪")
            return
        log_path = self._on_log_start()
        self._refresh_log_hint(log_path)
        if not silent:
            self._set_status(f"已开始记录 → {log_path}")

    def _export_log(
        self,
        silent: bool = False,
        from_stop: bool = False,
    ) -> None:
        if self._on_log_export is None:
            self._set_status("导出功能未就绪")
            return
        files = self._on_log_export()
        if from_stop and not silent:
            self._set_status(f"ANGLE_STOP 已发送；已导出 {files}")
        elif not silent:
            self._set_status(f"已导出 {files}")

    def _refresh_log_hint(self, log_path: Optional[str] = None) -> None:
        base = "建议勾选「角度环」「电机 RPM」订阅后再测试。"
        if log_path:
            self._log_hint.setText(f"导出目录：{log_path}。{base}")
        else:
            self._log_hint.setText(f"导出目录：工具当前目录下的 log 文件夹。{base}")

    def _step_default_yaw(self) -> None:
        self._delta_yaw_spin.setValue(self.DEFAULT_DELTA)
        self._base_rpm.setValue(self.DEFAULT_BASE_RPM)
        self._apply_angle()

    def _clear_plot(self) -> None:
        if self._on_clear_plot is not None:
            self._on_clear_plot()

    def _update_calib_available(self, info=None) -> None:
        if info is None:
            info = self._worker.hello_info
        enabled = info is not None and bool(info.caps & int(proto.Cap.YAW_CALIB))
        self._btn_calib_yaw.setEnabled(enabled)
        self._calib_yaw_spin.setEnabled(enabled)

    def _calib_yaw(self) -> None:
        if not self._btn_calib_yaw.isEnabled():
            self._set_status("固件不支持 YAW_CALIB")
            return
        ref_yaw = normalize_yaw(self._calib_yaw_spin.value())
        reply = QMessageBox.question(
            self,
            "校准物理 Yaw",
            f"请确认车辆已静止放平、电机已停止。\n\n"
            f"将把当前车头朝向设为 {ref_yaw:+d}°。\n\n是否继续？",
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No,
            QMessageBox.StandardButton.No,
        )
        if reply != QMessageBox.StandardButton.Yes:
            return
        self._emit_log_command("calib_yaw", note=f"ref={ref_yaw}")
        self._worker.request_calib_yaw(ref_yaw)
        self._set_status(f"CALIB_YAW 已发送 ref={ref_yaw:+d}°")

    def _set_status(self, text: str) -> None:
        self._status.setText(text)

    # ------------------------------------------------------------------
    # 自动测试序列
    # ------------------------------------------------------------------

    def _run_test_sequence(self) -> None:
        """启动自动测试序列（在后台线程中运行）。"""
        if not self._worker.is_connected():
            self._set_status("请先连接设备")
            return

        if self._test_running:
            self._set_status("测试已在运行中")
            return

        # 确保记录已开始
        if self._on_log_start is not None:
            self._start_log(silent=True)

        # 获取 recorder
        recorder = None
        if self._on_get_recorder is not None:
            recorder = self._on_get_recorder()
        if recorder is None:
            self._set_status("记录器未就绪，请先开始记录")
            return

        # 获取 PID 参数
        pid_params: dict = {}
        if self._on_get_pid_params is not None:
            pid_params = self._on_get_pid_params()

        # 选择测试序列
        mode = self._test_mode_combo.currentData()
        if mode == "quick":
            angles = list(QUICK_TEST_ANGLES)
        else:
            angles = list(DEFAULT_TEST_ANGLES)

        config = TestSequenceConfig(
            angles=angles,
            base_rpm=self._base_rpm.value(),
            max_turn_rpm=self._max_turn.value(),
            maneuver_timeout_s=30.0,
            settle_wait_s=1.5,
            inter_maneuver_delay_s=1.0,
        )

        self._test_running = True
        self._btn_run_test.setEnabled(False)
        self._btn_abort_test.setEnabled(True)
        self._test_progress.setText("正在运行测试序列...")

        def _run() -> None:
            runner = AngleTestRunner(
                send_fn=self._worker.request_set_angle,
                stop_fn=self._worker.request_angle_stop,
                recorder=recorder,
                pid_yaw_kp=pid_params.get("kp"),
                pid_yaw_ki=pid_params.get("ki"),
                pid_yaw_kd=pid_params.get("kd"),
                spd_limit_max_rpm=pid_params.get("max_rpm"),
            )
            runner.on_log = lambda msg: self.test_log.emit(msg)
            self._test_runner = runner
            result = runner.run_sequence(config)
            self.test_finished.emit(result)

        thread = threading.Thread(target=_run, daemon=True)
        thread.start()

    def _abort_test(self) -> None:
        """中断正在运行的测试。"""
        if self._test_runner is not None:
            self._test_runner.abort()
        self._test_running = False
        self._btn_run_test.setEnabled(True)
        self._btn_abort_test.setEnabled(False)
        self._test_progress.setText("测试已中断")
        self._set_status("测试已中断")

    def _on_test_finished(self, result: TestSequenceResult) -> None:
        """测试完成回调（由 test_finished 信号触发）。"""
        self._test_running = False
        self._btn_run_test.setEnabled(True)
        self._btn_abort_test.setEnabled(False)
        self._test_runner = None

        summary = (
            f"测试完成: {result.passed}/{len(result.steps)} 通过\n"
            f"CSV: {Path(result.csv_path).name if result.csv_path else '—'}\n"
            f"报告: {Path(result.report_path).name if result.report_path else '—'}"
        )
        self._test_progress.setText(summary)

        # 自动导出
        if self._chk_auto_export.isChecked():
            self._export_log(silent=False, from_stop=True)

        self._set_status(f"测试完成: {result.passed}/{len(result.steps)} 通过")

        # 显示快速摘要对话框
        lines = []
        for s in result.steps:
            status = "✓" if s.success else "✗"
            if s.metrics is not None:
                m = s.metrics
                rise = f"上升={m.rise_time_s:.1f}s" if m.rise_time_s is not None else "上升=—"
                settle = f" 稳态误差={m.steady_state_error_deg:.1f}°" if m.completed else ""
                lines.append(f"  {s.label}: {status} {rise}{settle}")
            else:
                lines.append(f"  {s.label}: {status} (无指标)")
        details = "\n".join(lines) if lines else "无详情"
        QMessageBox.information(
            self,
            "角度环自动测试结果",
            f"{summary}\n\n详情:\n{details}",
        )

    def set_test_result_callback(self, callback: Callable[[TestSequenceResult], None]) -> None:
        """设置测试完成后的回调（用于外部处理报告）。"""
        if not self._test_running:
            self.test_finished.connect(callback)
        # 避免重复连接
        try:
            self.test_finished.disconnect(self._on_test_finished)
        except (TypeError, RuntimeError):
            pass
        self.test_finished.connect(self._on_test_finished)
        self.test_finished.connect(callback)
