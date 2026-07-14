"""角度环自动测试序列。

通过蓝牙串口自动执行一组 SET_ANGLE 阶跃测试，记录全程数据并生成分析报告。
"""

from __future__ import annotations

import json
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Callable, Optional

import tm_proto as proto
from angle_log_export import (
    AngleLoopRecorder,
    AngleTestReport,
    ManeuverMetrics,
    _stamp_for_filename,
    _wall_iso,
    log_dir,
)


def _normalize_180(v: int) -> int:
    """归一化到 (-180, 180]，与固件 chassis_wrap_yaw_deg 对齐。"""
    v = v % 360
    if v > 180:
        v -= 360
    if v <= -180:
        v += 360
    return v


# 默认测试序列：从小到大、正负交替
DEFAULT_TEST_ANGLES = [
    (+30,  "小角度正向 30°"),
    (-30,  "小角度反向 -30°"),
    (+45,  "中角度正向 45°"),
    (-45,  "中角度反向 -45°"),
    (+90,  "大角度正向 90°"),
    (-90,  "大角度反向 -90°"),
    (+180, "极限正向 180°"),
    (-180, "极限反向 -180°"),
]

# 快速验证序列
QUICK_TEST_ANGLES = [
    (+45,  "快速 45°"),
    (-45,  "快速 -45°"),
    (+90,  "快速 90°"),
]


@dataclass
class TestSequenceConfig:
    angles: list[tuple[int, str]] = field(default_factory=lambda: list(DEFAULT_TEST_ANGLES))
    base_rpm: int = 0                 # 基准 RPM（0=原地转向）
    max_turn_rpm: int = 0             # 差速上限（0=默认）
    maneuver_timeout_s: float = 30.0  # 单次机动超时
    settle_wait_s: float = 2.0        # 机动完成后稳定等待
    inter_maneuver_delay_s: float = 1.5  # 机动间隔


@dataclass
class TestStepResult:
    target_yaw: int
    label: str
    success: bool
    metrics: Optional[ManeuverMetrics] = None
    error_msg: str = ""


@dataclass
class TestSequenceResult:
    started_at: str = ""
    finished_at: str = ""
    config: TestSequenceConfig = field(default_factory=TestSequenceConfig)
    steps: list[TestStepResult] = field(default_factory=list)
    report: Optional[AngleTestReport] = None
    csv_path: str = ""
    report_path: str = ""

    @property
    def passed(self) -> int:
        return sum(1 for s in self.steps if s.success)

    @property
    def failed(self) -> int:
        return len(self.steps) - self.passed


class AngleTestRunner:
    """角度环自动测试器。

    用法:
        runner = AngleTestRunner(
            send_fn=worker.request_set_angle,
            stop_fn=worker.request_angle_stop,
            recorder=angle_recorder,
        )
        runner.on_log = lambda msg: print(msg)
        result = runner.run_sequence()
    """

    def __init__(
        self,
        *,
        send_fn: Callable[[bytes], None],
        stop_fn: Callable[[], None],
        recorder: AngleLoopRecorder,
        pid_yaw_kp: Optional[float] = None,
        pid_yaw_ki: Optional[float] = None,
        pid_yaw_kd: Optional[float] = None,
        spd_limit_max_rpm: Optional[float] = None,
    ):
        self._send = send_fn
        self._stop = stop_fn
        self._recorder = recorder
        self._pid_kp = pid_yaw_kp
        self._pid_ki = pid_yaw_ki
        self._pid_kd = pid_yaw_kd
        self._max_rpm = spd_limit_max_rpm
        self.on_log: Callable[[str], None] = lambda msg: None
        self._abort = False

    def abort(self) -> None:
        self._abort = True

    def run_sequence(
        self,
        config: Optional[TestSequenceConfig] = None,
    ) -> TestSequenceResult:
        """执行完整的测试序列，返回汇总结果。"""
        if config is None:
            config = TestSequenceConfig()

        self._abort = False
        result = TestSequenceResult(
            started_at=_wall_iso(),
            config=config,
        )

        self.on_log(f"=== 角度环自动测试开始 ({len(config.angles)} 个角度) ===")
        self.on_log(f"base_rpm={config.base_rpm}, max_turn={config.max_turn_rpm}, "
                     f"超时={config.maneuver_timeout_s}s")

        for target_yaw, label in config.angles:
            if self._abort:
                self.on_log("测试被中断")
                break

            self.on_log(f"\n--- [{label}] Δθ={target_yaw:+d}° ---")
            step = self._run_single_maneuver(target_yaw, label, config)
            result.steps.append(step)

            if step.success and step.metrics:
                m = step.metrics
                rise = f"上升={m.rise_time_s:.1f}s" if m.rise_time_s is not None else "上升=—"
                settle = f" 稳定={m.settling_time_s:.1f}s" if m.settling_time_s is not None else ""
                self.on_log(
                    f"  结果: {'✓ 到位' if m.completed else '✗ 未到位'} | "
                    f"{rise}{settle}"
                    f" | 超调={m.overshoot_deg:.1f}° ({m.overshoot_pct:.0f}%) | "
                    f"稳态误差={m.steady_state_error_deg:.1f}° | "
                    f"max_turn={m.max_turn_rpm:.0f} rpm"
                )

            if not step.success:
                self.on_log(f"  失败: {step.error_msg}")

        # 停止记录并导出
        self._recorder.stop()

        # 生成报告
        report = self._recorder.build_report(
            pid_kp=self._pid_kp,
            pid_ki=self._pid_ki,
            pid_kd=self._pid_kd,
            max_rpm=self._max_rpm,
            timeout_s=config.maneuver_timeout_s,
        )
        result.report = report

        # 导出 CSV + JSON
        csv_path, _ = self._recorder.export()
        result.csv_path = str(csv_path)

        # 导出汇总报告 JSON
        stamp = _stamp_for_filename()
        report_json_path = log_dir() / f"angle_test_report_{stamp}.json"
        report_data = self._serialize_report(result)
        report_json_path.write_text(
            json.dumps(report_data, ensure_ascii=False, indent=2),
            encoding="utf-8",
        )
        result.report_path = str(report_json_path)

        result.finished_at = _wall_iso()

        self.on_log(f"\n=== 测试完成: {result.passed}/{len(result.steps)} 通过 ===")
        self.on_log(f"CSV: {csv_path}")
        self.on_log(f"报告: {report_json_path}")

        return result

    def _run_single_maneuver(
        self,
        target_yaw: int,
        label: str,
        config: TestSequenceConfig,
    ) -> TestStepResult:
        """执行单次 SET_ANGLE 并等待结果。"""
        # 确保记录器已启动
        if not self._recorder.active:
            return TestStepResult(
                target_yaw=target_yaw,
                label=label,
                success=False,
                error_msg="记录器未启动",
            )

        # 先停止之前的机动
        self._stop()
        time.sleep(0.3)

        # 标记机动开始
        self._recorder.record_command(
            "set_angle",
            target=target_yaw,
            base=config.base_rpm,
            max_turn=config.max_turn_rpm,
            note=f"auto_test: {label}",
        )

        # 下发 SET_ANGLE
        payload = proto.build_set_angle(target_yaw, config.base_rpm, config.max_turn_rpm)
        self._send(payload)
        self.on_log(f"  SET_ANGLE Δθ={target_yaw:+d}° 已发送")

        # 等待机动完成或超时
        deadline = time.monotonic() + config.maneuver_timeout_s
        settle_deadline: Optional[float] = None
        settled = False
        started = False  # 角度环是否已开始（target 变为请求值）

        while time.monotonic() < deadline:
            if self._abort:
                self._stop()
                return TestStepResult(
                    target_yaw=target_yaw,
                    label=label,
                    success=False,
                    error_msg="用户中断",
                )

            time.sleep(0.1)

            # 简单判断：最近一次 angle_loop 推送的误差是否在容差内
            session = self._recorder.session
            angle_rows = [r for r in session.rows if r.get("kind") == "angle_loop"]
            if not angle_rows:
                continue

            last = angle_rows[-1]
            tgt = last.get("target_yaw")
            cur = last.get("current_yaw")
            if tgt == "" or cur == "":
                continue

            i_tgt = int(tgt)
            i_cur = int(cur)

            # 检测角度环已启动（target 变为请求值，±180° 归一化比较）
            if not started:
                if _normalize_180(i_tgt) == _normalize_180(target_yaw):
                    started = True
                    self.on_log(f"  角度环已启动 target={i_tgt}")
                continue  # 启动后下一帧再判断到位

            # 固件已完成：push 归零 (target=0, current=0)
            if i_tgt == 0 and i_cur == 0:
                self.on_log(f"  固件报告角度环已完成 (target/current 归零)")
                settled = True
                break

            err = abs(i_tgt - i_cur)

            # 容差计算（与固件 chassis_angle_done_tol_deg 12% 一致）
            a = abs(target_yaw)
            tol = a * 0.12
            if tol < 5.0:
                tol = 5.0
            cap_val = a * 0.60
            if tol > cap_val:
                tol = max(3.0, cap_val)
            if tol < 3.0:
                tol = 3.0

            if err <= tol:
                if settle_deadline is None:
                    settle_deadline = time.monotonic() + config.settle_wait_s
                    self.on_log(f"  进入容差 (err={err}° ≤ tol={tol:.1f}°)，稳定等待 {config.settle_wait_s}s...")
                elif time.monotonic() >= settle_deadline:
                    settled = True
                    break
            else:
                settle_deadline = None  # 重新计时

        # 停止机动
        self._recorder.record_command("angle_stop", note=f"auto_test_end: {label}")
        self._stop()

        # 计算本段指标
        metrics = None
        # 找到本次机动对应的行范围
        cmd_indices = [
            i for i, r in enumerate(self._recorder.session.rows)
            if r.get("kind") == "cmd_set_angle" and r.get("note", "").endswith(label)
        ]
        if cmd_indices:
            start_idx = cmd_indices[-1]  # 取最后一个匹配的
            # 找对应的 angle_stop 或到末尾
            stop_indices = [
                i for i, r in enumerate(self._recorder.session.rows)
                if i > start_idx and r.get("kind") in ("cmd_angle_stop", "record_stop")
            ]
            end_idx = stop_indices[0] if stop_indices else len(self._recorder.session.rows) - 1
            metrics = self._recorder.session.compute_maneuver_metrics(
                target_yaw, start_idx, end_idx, config.maneuver_timeout_s,
            )

        time.sleep(config.inter_maneuver_delay_s)

        if settled:
            return TestStepResult(
                target_yaw=target_yaw,
                label=label,
                success=True,
                metrics=metrics,
            )
        else:
            return TestStepResult(
                target_yaw=target_yaw,
                label=label,
                success=False,
                metrics=metrics,
                error_msg=f"超时 ({config.maneuver_timeout_s}s 内未稳定在容差内)",
            )

    @staticmethod
    def _serialize_report(result: TestSequenceResult) -> dict[str, Any]:
        """将测试结果序列化为 JSON-friendly 字典。"""
        data: dict[str, Any] = {
            "started_at": result.started_at,
            "finished_at": result.finished_at,
            "config": {
                "base_rpm": result.config.base_rpm,
                "max_turn_rpm": result.config.max_turn_rpm,
                "maneuver_timeout_s": result.config.maneuver_timeout_s,
            },
            "summary": {
                "total": len(result.steps),
                "passed": result.passed,
                "failed": result.failed,
            },
            "steps": [],
        }

        if result.report:
            data["pid"] = {
                "kp": result.report.pid_yaw_kp,
                "ki": result.report.pid_yaw_ki,
                "kd": result.report.pid_yaw_kd,
                "spd_limit_max_rpm": result.report.spd_limit_max_rpm,
            }

        for step in result.steps:
            step_data: dict[str, Any] = {
                "target_yaw": step.target_yaw,
                "label": step.label,
                "success": step.success,
                "error": step.error_msg,
            }
            if step.metrics:
                m = step.metrics
                step_data["metrics"] = {
                    "completed": m.completed,
                    "timed_out": m.timed_out,
                    "rise_time_s": m.rise_time_s,
                    "settling_time_s": m.settling_time_s,
                    "overshoot_deg": round(m.overshoot_deg, 2),
                    "overshoot_pct": round(m.overshoot_pct, 1),
                    "steady_state_error_deg": round(m.steady_state_error_deg, 2),
                    "max_turn_rpm": round(m.max_turn_rpm, 1),
                    "sample_count": m.sample_count,
                }
            data["steps"].append(step_data)

        return data


def build_test_report_json(csv_path: Path, pid_params: dict[str, float]) -> str:
    """离线分析：从已有 CSV 构建测试报告 JSON。

    可用于已经导出 CSV 但未运行 test runner 的场景。
    """
    import csv as csv_mod

    from angle_log_export import AngleLogSession

    session = AngleLogSession()
    with csv_path.open("r", encoding="utf-8") as fh:
        reader = csv_mod.DictReader(fh)
        for row in reader:
            session.rows.append(row)

    report = session.build_report(
        pid_kp=pid_params.get("kp"),
        pid_ki=pid_params.get("ki"),
        pid_kd=pid_params.get("kd"),
        max_rpm=pid_params.get("max_rpm"),
    )

    report_path = csv_path.with_suffix(".report.json")
    report_data: dict[str, Any] = {
        "csv_file": str(csv_path.name),
        "exported_at": _wall_iso(),
        "pid": pid_params,
        "maneuvers": [],
    }
    for m in report.maneuvers:
        report_data["maneuvers"].append({
            "target_yaw": m.target_yaw,
            "completed": m.completed,
            "rise_time_s": m.rise_time_s,
            "settling_time_s": m.settling_time_s,
            "overshoot_deg": round(m.overshoot_deg, 2),
            "overshoot_pct": round(m.overshoot_pct, 1),
            "steady_state_error_deg": round(m.steady_state_error_deg, 2),
            "max_turn_rpm": round(m.max_turn_rpm, 1),
        })

    report_path.write_text(
        json.dumps(report_data, ensure_ascii=False, indent=2),
        encoding="utf-8",
    )
    return str(report_path)
