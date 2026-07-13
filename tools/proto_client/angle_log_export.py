"""角度环调试会话记录与 CSV/JSON 导出（写入工具当前目录 log/）。"""

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
    "target_yaw",
    "current_yaw",
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
        self._append_row(
            kind="angle_loop",
            target_yaw=sample.target_yaw,
            current_yaw=sample.current_yaw,
            turn_rpm=sample.turn_rpm,
            base_rpm=sample.base_rpm,
            yaw_meas_deg=round(yaw_meas_deg, 3),
            yaw_target_deg=round(yaw_target_deg, 3),
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
        row = {col: "" for col in CSV_COLUMNS}
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

    def export(self) -> tuple[Path, Path]:
        if not self.rows:
            self._append_row(kind="export_empty", note="no samples; exported metadata only")

        stamp = _stamp_for_filename()
        out_dir = log_dir()
        csv_path = out_dir / f"angle_debug_{stamp}.csv"
        meta_path = out_dir / f"angle_debug_{stamp}_meta.json"

        with csv_path.open("w", newline="", encoding="utf-8") as fh:
            writer = csv.DictWriter(fh, fieldnames=CSV_COLUMNS, extrasaction="ignore")
            writer.writeheader()
            writer.writerows(self.rows)

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
