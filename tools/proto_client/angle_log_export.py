"""角度环调试会话记录与 CSV/JSON 导出（写入工具当前目录 log/）。

增强功能：
- 自动计算 yaw_error 列
- 分段检测 (ramp / settle / done / timeout)
- 每个机动结束后生成汇总指标 JSON
- 支持批量测试序列
"""

from __future__ import annotations

import csv
import json
import time
from dataclasses import dataclass, field
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Optional

import tm_proto as proto


def log_dir() -> Path:
    """工具运行时的当前工作目录下的 log 文件夹。"""
    path = Path.cwd() / "log"
    path.mkdir(parents=True, exist_ok=True)
    return path


def _wall_iso() -> str:
    return datetime.now(timezone.utc).astimezone().isoformat(timespec="milliseconds")


def _stamp_for_filename() -> str:
    return datetime.now().strftime("%Y%m%d_%H%M%S")


CSV_COLUMNS = [
    "t_s",
    "wall_time",
    "kind",
    "segment",
    "target_yaw",
    "current_yaw",
    "yaw_error",
    "turn_rpm",
    "base_rpm",
    "yaw_meas_deg",
    "yaw_target_deg",
    "att_roll",
    "att_pitch",
    "att_yaw",
    "rpm_m1",
    "rpm_m2",
    "rpm_m3",
    "rpm_m4",
    "enc_m1",
    "enc_m2",
    "enc_m3",
    "enc_m4",
    "cmd_target",
    "cmd_base",
    "cmd_max_turn",
    "note",
]


@dataclass
class ManeuverMetrics:
    """单次角度机动的性能指标。"""
    target_yaw: int = 0
    # 时间指标
    t_start: float = 0.0
    t_first_reach: Optional[float] = None       # 首次进入容差的时间
    t_settle: Optional[float] = None             # 最终稳定在容差内的时间
    t_end: float = 0.0
    # 性能指标
    rise_time_s: Optional[float] = None          # 首次到达目标的时间
    settling_time_s: Optional[float] = None      # 稳定时间 (5% 容差带)
    overshoot_deg: float = 0.0                   # 最大超调量 (度)
    overshoot_pct: float = 0.0                   # 超调百分比
    steady_state_error_deg: float = 0.0          # 稳态误差 (最后 0.5s 均值)
    max_turn_rpm: float = 0.0                    # 最大转向 RPM
    # 结果
    completed: bool = False                      # 是否在机动时限内到位
    timed_out: bool = False                      # 是否超时
    sample_count: int = 0


@dataclass
class AngleTestReport:
    """一次完整测试的汇总报告。"""
    exported_at: str = ""
    log_dir: str = ""
    csv_file: str = ""
    recording_started_at: str = ""
    motor_count: int = proto.MOTOR_COUNT_DEFAULT
    angle_subscribed: bool = False
    rpm_subscribed: bool = False
    pid_yaw_kp: Optional[float] = None
    pid_yaw_ki: Optional[float] = None
    pid_yaw_kd: Optional[float] = None
    spd_limit_max_rpm: Optional[float] = None
    maneuvers: list[ManeuverMetrics] = field(default_factory=list)
    row_count: int = 0
    commands: list[dict[str, Any]] = field(default_factory=list)
    sample_counts: dict[str, int] = field(default_factory=dict)


@dataclass
class AngleLogSession:
    started_wall: str = ""
    motor_count: int = proto.MOTOR_COUNT_MAX
    angle_subscribed: bool = False
    rpm_subscribed: bool = False
    commands: list[dict[str, Any]] = field(default_factory=list)
    rows: list[dict[str, Any]] = field(default_factory=list)
    time_origin: Optional[float] = None

    def _t_s(self) -> float:
        now = time.monotonic()
        if self.time_origin is None:
            self.time_origin = now
        return now - self.time_origin

    def start(self, motor_count: int, angle_subscribed: bool, rpm_subscribed: bool) -> None:
        self.started_wall = _wall_iso()
        self.motor_count = motor_count
        self.angle_subscribed = angle_subscribed
        self.rpm_subscribed = rpm_subscribed
        self.commands.clear()
        self.rows.clear()
        self.time_origin = time.monotonic()
        self._append_row(kind="record_start", note="recording started")

    def record_command(
        self,
        name: str,
        *,
        target: Optional[int] = None,
        base: Optional[int] = None,
        max_turn: Optional[int] = None,
        note: str = "",
    ) -> None:
        cmd = {
            "wall_time": _wall_iso(),
            "t_s": round(self._t_s(), 6),
            "name": name,
            "target": target,
            "base": base,
            "max_turn": max_turn,
            "note": note,
        }
        self.commands.append(cmd)
        self._append_row(
            kind=f"cmd_{name}",
            cmd_target=target,
            cmd_base=base,
            cmd_max_turn=max_turn,
            note=note or name,
        )

    def record_angle_loop(
        self,
        sample: proto.AngleLoopPush,
        yaw_meas_deg: float,
        yaw_target_deg: float,
    ) -> None:
        yaw_err = sample.target_yaw - sample.current_yaw
        self._append_row(
            kind="angle_loop",
            target_yaw=sample.target_yaw,
            current_yaw=sample.current_yaw,
            yaw_error=yaw_err,
            turn_rpm=sample.turn_rpm,
            base_rpm=sample.base_rpm,
            yaw_meas_deg=round(yaw_meas_deg, 3),
            yaw_target_deg=round(yaw_target_deg, 3),
            enc_m1=sample.enc[0],
            enc_m2=sample.enc[1],
            enc_m3=sample.enc[2],
            enc_m4=sample.enc[3],
        )

    def record_attitude(self, roll: float, pitch: float, yaw: float) -> None:
        self._append_row(
            kind="attitude",
            att_roll=round(roll, 3),
            att_pitch=round(pitch, 3),
            att_yaw=round(yaw, 3),
        )

    def record_motor_rpm(self, rpms: tuple[int, int, int, int]) -> None:
        self._append_row(
            kind="motor_rpm",
            rpm_m1=rpms[0],
            rpm_m2=rpms[1],
            rpm_m3=rpms[2],
            rpm_m4=rpms[3],
        )

    def record_encoder(self, counts: tuple[int, int, int, int]) -> None:
        self._append_row(
            kind="encoder",
            enc_m1=counts[0],
            enc_m2=counts[1],
            enc_m3=counts[2],
            enc_m4=counts[3],
        )

    def _append_row(self, kind: str, note: str = "", **fields: Any) -> None:
        row: dict[str, Any] = {col: "" for col in CSV_COLUMNS}
        row["t_s"] = round(self._t_s(), 6)
        row["wall_time"] = _wall_iso()
        row["kind"] = kind
        row["note"] = note
        for key, val in fields.items():
            if key in row and val is not None:
                row[key] = val
        self.rows.append(row)

    def sample_counts(self) -> dict[str, int]:
        counts: dict[str, int] = {}
        for row in self.rows:
            kind = str(row.get("kind", ""))
            counts[kind] = counts.get(kind, 0) + 1
        return counts

    # ------------------------------------------------------------------
    # 机动分段与分析
    # ------------------------------------------------------------------

    @staticmethod
    def _angle_rows(rows: list[dict[str, Any]]) -> list[dict[str, Any]]:
        """提取所有 angle_loop 行（含 yaw_error）。"""
        return [r for r in rows if r.get("kind") == "angle_loop" and r.get("yaw_error") != ""]

    @staticmethod
    def _detect_maneuvers(rows: list[dict[str, Any]]) -> list[tuple[int, int]]:
        """通过 cmd_set_angle 行检测每次机动起止索引，返回 [(start_idx, end_idx), ...]."""
        cmd_indices = [
            i for i, r in enumerate(rows)
            if r.get("kind") in ("cmd_set_angle", "cmd_angle_stop", "record_stop")
        ]
        maneuvers: list[tuple[int, int]] = []
        seg_start: Optional[int] = None
        for idx in cmd_indices:
            kind = rows[idx].get("kind", "")
            if kind == "cmd_set_angle" and seg_start is None:
                seg_start = idx
            elif kind in ("cmd_angle_stop", "record_stop") and seg_start is not None:
                maneuvers.append((seg_start, idx))
                seg_start = None
        # 如果没有显式 stop，用最后一行
        if seg_start is not None:
            maneuvers.append((seg_start, len(rows) - 1))
        return maneuvers

    def compute_maneuver_metrics(
        self,
        target_yaw: int,
        start_idx: int,
        end_idx: int,
        timeout_s: float = 90.0,
    ) -> ManeuverMetrics:
        """对指定行范围计算一次机动的全部性能指标。"""
        metrics = ManeuverMetrics(target_yaw=target_yaw)
        rows = self.rows[start_idx : end_idx + 1]
        angle_rows = self._angle_rows(rows)

        if not angle_rows:
            return metrics

        metrics.sample_count = len(angle_rows)

        # 容差（与固件 chassis_angle_done_tol_deg 一致）
        a = abs(target_yaw)
        tol = a * 0.25
        if tol < 8.0:
            tol = 8.0
        cap = a * 0.75
        if tol > cap:
            tol = max(3.0, cap)
        if tol < 3.0:
            tol = 3.0

        t_start = float(angle_rows[0].get("t_s", 0))
        t_end_val = float(angle_rows[-1].get("t_s", 0))
        metrics.t_start = t_start
        metrics.t_end = t_end_val

        # 遍历找各种指标
        first_reach_t: Optional[float] = None
        settled_t: Optional[float] = None
        max_overshoot = 0.0
        max_turn = 0.0
        settle_band = max(tol * 1.2, 5.0)  # 稳定带：1.2 倍容差，最低 5°
        settle_count = 0
        settle_required = 3  # 连续 N 帧在稳定带内才算稳定
        last_errors: list[float] = []

        for row in angle_rows:
            t = float(row.get("t_s", 0))
            err = float(row.get("yaw_error", 0))
            turn = abs(float(row.get("turn_rpm", 0)))
            last_errors.append(err)

            if turn > max_turn:
                max_turn = turn

            # 超调：目标为正时 current > target，目标为负时 current < target
            overshoot = 0.0
            if target_yaw > 0:
                overshoot = float(row.get("current_yaw", 0)) - target_yaw
            elif target_yaw < 0:
                overshoot = target_yaw - float(row.get("current_yaw", 0))
            if overshoot > max_overshoot:
                max_overshoot = overshoot

            # 首次进入容差
            if first_reach_t is None and abs(err) <= tol:
                first_reach_t = t

            # 稳定检测
            if abs(err) <= settle_band:
                settle_count += 1
                if settle_count >= settle_required and settled_t is None:
                    settled_t = t - (settle_required - 1) * 0.02  # 回推到第一帧
            else:
                settle_count = 0
                settled_t = None  # 重新计数

        # 填充指标
        if first_reach_t is not None:
            metrics.t_first_reach = first_reach_t
            metrics.rise_time_s = first_reach_t - t_start

        if settled_t is not None:
            metrics.t_settle = settled_t
            metrics.settling_time_s = settled_t - t_start

        metrics.max_turn_rpm = max_turn
        metrics.overshoot_deg = max_overshoot
        if abs(target_yaw) > 0:
            metrics.overshoot_pct = (max_overshoot / abs(target_yaw)) * 100.0
        else:
            metrics.overshoot_pct = 0.0

        # 稳态误差：最后 0.5s 的平均误差
        if last_errors:
            tail_count = max(1, len(last_errors) // 4)  # 最后 25% 的样本
            tail_errors = last_errors[-tail_count:]
            metrics.steady_state_error_deg = sum(tail_errors) / len(tail_errors)

        # 完成判定
        if first_reach_t is not None and settled_t is not None:
            metrics.completed = True
        elif (t_end_val - t_start) >= timeout_s:
            metrics.timed_out = True
            metrics.completed = False

        # 如果轨迹最终在容差内，也算完成
        if last_errors and abs(last_errors[-1]) <= tol:
            metrics.completed = True

        return metrics

    def build_report(
        self,
        pid_kp: Optional[float] = None,
        pid_ki: Optional[float] = None,
        pid_kd: Optional[float] = None,
        max_rpm: Optional[float] = None,
        timeout_s: float = 90.0,
    ) -> AngleTestReport:
        """对整个会话进行分析，生成汇总报告。"""
        report = AngleTestReport(
            exported_at=_wall_iso(),
            log_dir=str(log_dir().resolve()),
            recording_started_at=self.started_wall,
            motor_count=self.motor_count,
            angle_subscribed=self.angle_subscribed,
            rpm_subscribed=self.rpm_subscribed,
            pid_yaw_kp=pid_kp,
            pid_yaw_ki=pid_ki,
            pid_yaw_kd=pid_kd,
            spd_limit_max_rpm=max_rpm,
            row_count=len(self.rows),
            commands=self.commands,
            sample_counts=self.sample_counts(),
        )

        manif = self._detect_maneuvers(self.rows)
        for start_idx, end_idx in manif:
            # 取机动开始时的目标角度
            target = 0
            for r in self.rows[start_idx:end_idx + 1]:
                t = r.get("cmd_target")
                if t is not None and t != "":
                    target = int(t)
                    break
            if target == 0:
                continue

            metrics = self.compute_maneuver_metrics(target, start_idx, end_idx, timeout_s)
            report.maneuvers.append(metrics)

        return report

    # ------------------------------------------------------------------
    # 导出
    # ------------------------------------------------------------------

    def export(self) -> tuple[Path, Path]:
        if not self.rows:
            self._append_row(kind="export_empty", note="no samples; exported metadata only")

        stamp = _stamp_for_filename()
        out_dir = log_dir()
        csv_path = out_dir / f"angle_debug_{stamp}.csv"
        meta_path = out_dir / f"angle_debug_{stamp}_meta.json"

        # ---- 重写 CSV，补齐 yaw_error ----
        with csv_path.open("w", newline="", encoding="utf-8") as fh:
            writer = csv.DictWriter(fh, fieldnames=CSV_COLUMNS, extrasaction="ignore")
            writer.writeheader()
            for row in self.rows:
                if row.get("kind") == "angle_loop" and row.get("yaw_error") == "":
                    tgt = row.get("target_yaw")
                    cur = row.get("current_yaw")
                    if tgt != "" and cur != "":
                        row["yaw_error"] = int(tgt) - int(cur)
                writer.writerow(row)

        meta = {
            "exported_at": _wall_iso(),
            "log_dir": str(out_dir.resolve()),
            "csv_file": csv_path.name,
            "recording_started_at": self.started_wall,
            "motor_count": self.motor_count,
            "angle_subscribed": self.angle_subscribed,
            "rpm_subscribed": self.rpm_subscribed,
            "sample_counts": self.sample_counts(),
            "row_count": len(self.rows),
            "commands": self.commands,
        }
        meta_path.write_text(json.dumps(meta, ensure_ascii=False, indent=2), encoding="utf-8")
        return csv_path, meta_path


class AngleLoopRecorder:
    """角度环调试记录器：仅在 active 时写入 session。"""

    def __init__(self) -> None:
        self._active = False
        self._session = AngleLogSession()

    @property
    def active(self) -> bool:
        return self._active

    @property
    def row_count(self) -> int:
        return len(self._session.rows)

    @property
    def session(self) -> AngleLogSession:
        return self._session

    def start(
        self,
        motor_count: int,
        angle_subscribed: bool,
        rpm_subscribed: bool,
    ) -> None:
        self._session.start(motor_count, angle_subscribed, rpm_subscribed)
        self._active = True

    def stop(self) -> None:
        if self._active:
            self._session._append_row(kind="record_stop", note="recording stopped")
        self._active = False

    def record_command(self, name: str, **kwargs: Any) -> None:
        if not self._active:
            return
        self._session.record_command(name, **kwargs)

    def record_angle_loop(
        self,
        sample: proto.AngleLoopPush,
        yaw_meas_deg: float,
        yaw_target_deg: float,
    ) -> None:
        if not self._active:
            return
        self._session.record_angle_loop(sample, yaw_meas_deg, yaw_target_deg)

    def record_attitude(self, roll: float, pitch: float, yaw: float) -> None:
        if not self._active:
            return
        self._session.record_attitude(roll, pitch, yaw)

    def record_motor_rpm(self, rpms: tuple[int, int, int, int]) -> None:
        if not self._active:
            return
        self._session.record_motor_rpm(rpms)

    def record_encoder(self, counts: tuple[int, int, int, int]) -> None:
        if not self._active:
            return
        self._session.record_encoder(counts)

    def export(self) -> tuple[Path, Path]:
        return self._session.export()

    def build_report(self, **kwargs: Any) -> AngleTestReport:
        """生成测试汇总报告（含每段机动的性能指标）。"""
        return self._session.build_report(**kwargs)
