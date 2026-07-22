"""遥控：八方向 DRIVE、地图点击导航、物理 Yaw 校准。"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Optional

import tm_proto as proto
# removed MapOdometry,MapPose
from PySide6.QtCore import Qt
from PySide6.QtGui import QFocusEvent
from PySide6.QtWidgets import (
    QApplication,
    QFormLayout,
    QGridLayout,
    QGroupBox,
    QHBoxLayout,
    QLabel,
    QMessageBox,
    QPushButton,
    QSpinBox,
    QSplitter,
    QVBoxLayout,
    QWidget,
)

from serial_worker import NavResult, SerialWorker
# local normalize_yaw function
def _normalize_yaw_local(d):
    d=int(d)%360
    if d>180: d-=360
    if d<=-180: d+=360
    return d
from ui.drive_map_widget import DriveJoystick


@dataclass(frozen=True)
class DriveVector:
    throttle: int
    steer: int


def _diag(magnitude: int) -> int:
    return max(1, int(magnitude * 0.707))


DIRECTION_VECTORS: dict[str, DriveVector] = {
    "fwd": DriveVector(1, 0),
    "back": DriveVector(-1, 0),
    "left": DriveVector(0, -1),
    "right": DriveVector(0, 1),
    "fwd_left": DriveVector(1, -1),
    "fwd_right": DriveVector(1, 1),
    "back_left": DriveVector(-1, -1),
    "back_right": DriveVector(-1, 1),
}


class DriveTab(QWidget):
    BTN_SIZE = 64

    def __init__(self, worker: SerialWorker, parent: Optional[QWidget] = None):
        super().__init__(parent)
        self._worker = worker
        self._caps = 0
        self._motor_count = proto.MOTOR_COUNT_DEFAULT
        self._active_key: Optional[str] = None
        self._direction_buttons: dict[str, QPushButton] = {}
        self._navigating = False
        # self._nav_id removed
        # self._nav_freeze_odom removed
        # self._last_enc removed
        # odom removed

        root = QHBoxLayout(self)
        splitter = QSplitter(Qt.Orientation.Horizontal)

        map_page = QWidget()
        map_layout = QVBoxLayout(map_page)
        # joystick mode - no toolbar
        

        self._joystick = DriveJoystick()
        self._joystick.direction_activated.connect(self._on_joystick_direction)
        self._joystick.direction_released.connect(self._on_joystick_released)
        map_layout.addWidget(self._joystick, stretch=1)
        splitter.addWidget(map_page)

        ctrl_page = QWidget()
        ctrl_layout = QVBoxLayout(ctrl_page)
        ctrl_layout.addLayout(self._build_toolbar())
        ctrl_layout.addWidget(self._build_pad_group())
        ctrl_layout.addWidget(self._build_calib_group())
        ctrl_layout.addWidget(self._build_hint_group())
        self._status = QLabel("未连接")
        self._status.setWordWrap(True)
        ctrl_layout.addWidget(self._status)
        ctrl_layout.addStretch()
        splitter.addWidget(ctrl_page)

        splitter.setStretchFactor(0, 3)
        splitter.setStretchFactor(1, 2)
        root.addWidget(splitter)

        self._worker.connected_changed.connect(self._on_connected)
        self._worker.navigation_finished.connect(self._on_navigation_finished)

    def _build_toolbar(self) -> QHBoxLayout:
        row = QHBoxLayout()
        row.addWidget(QLabel("力度"))
        self._magnitude = QSpinBox()
        self._magnitude.setRange(100, proto.DRIVE_MAGNITUDE_MAX)
        self._magnitude.setSingleStep(50)
        self._magnitude.setValue(proto.DRIVE_MAGNITUDE_DEFAULT)
        self._magnitude.setSuffix(" /1000")
        row.addWidget(self._magnitude)
        row.addStretch()
        return row

    def _build_pad_group(self) -> QGroupBox:
        box = QGroupBox("方向操控（按住移动，松开停车）")
        layout = QVBoxLayout(box)

        grid = QGridLayout()
        grid.setSpacing(6)
        specs = [
            ("fwd_left", "↖", 0, 0),
            ("fwd", "↑", 0, 1),
            ("fwd_right", "↗", 0, 2),
            ("left", "←", 1, 0),
            ("stop", "■", 1, 1),
            ("right", "→", 1, 2),
            ("back_left", "↙", 2, 0),
            ("back", "↓", 2, 1),
            ("back_right", "↘", 2, 2),
        ]
        for key, label, row, col in specs:
            btn = QPushButton(label)
            btn.setFixedSize(self.BTN_SIZE, self.BTN_SIZE)
            btn.setFocusPolicy(Qt.FocusPolicy.NoFocus)
            if key == "stop":
                btn.setStyleSheet("font-size: 20px; font-weight: bold;")
                btn.clicked.connect(self._stop_drive)
            else:
                btn.setStyleSheet("font-size: 22px;")
                btn.pressed.connect(lambda k=key: self._on_direction_pressed(k))
                btn.released.connect(self._on_direction_released)
                self._direction_buttons[key] = btn
            grid.addWidget(btn, row, col)

        layout.addLayout(grid)
        return box

    def _build_nav_group(self) -> QGroupBox:
        box = QGroupBox("地图导航")
        form = QFormLayout(box)
        self._nav_turn_rpm = QSpinBox()
        self._nav_turn_rpm.setRange(20, 200)
        self._nav_turn_rpm.setValue(80)
        self._nav_drive_rpm = QSpinBox()
        self._nav_drive_rpm.setRange(20, 200)
        self._nav_drive_rpm.setValue(60)
        form.addRow("转向 RPM 上限", self._nav_turn_rpm)
        form.addRow("行驶 RPM 上限", self._nav_drive_rpm)
        hint = QLabel(
            "左键点击地图设目标；导航中再次点击会覆盖上一目标并立即重规划。"
            "固件按目标幅度计算到位容差并提前停车。"
        )
        hint.setWordWrap(True)
        hint.setStyleSheet("color: palette(mid);")
        form.addRow(hint)
        return box

    def _build_calib_group(self) -> QGroupBox:
        box = QGroupBox("物理世界校准")
        form = QFormLayout(box)
        self._calib_yaw_spin = QSpinBox()
        self._calib_yaw_spin.setRange(-180, 180)
        self._calib_yaw_spin.setSingleStep(15)
        self._calib_yaw_spin.setWrapping(True)
        self._calib_yaw_spin.setValue(0)
        self._calib_yaw_spin.setSuffix(" °")
        self._calib_yaw_spin.setToolTip(
            "校准后车头朝向为参考角，地图原点重置到当前位置（地图中心）"
        )
        self._btn_calib_yaw = QPushButton("校准并重置地图原点")
        row = QHBoxLayout()
        row.addWidget(self._calib_yaw_spin, stretch=1)
        row.addWidget(self._btn_calib_yaw)
        form.addRow("参考航向", row)
        self._btn_calib_yaw.clicked.connect(self._calib_yaw)
        return box

    def _build_hint_group(self) -> QGroupBox:
        box = QGroupBox("说明")
        layout = QVBoxLayout(box)
        self._hint = QLabel(
            "地图 +Y 为车前方向（校准后 0°）。位置由编码器积分估计，长按方向键可手动微调。\n"
            "「校准并重置地图原点」：当前物理位置变为地图中心，车头朝向设为参考角。\n"
            "需要固件支持 ANGLE_LOOP + DISTANCE_LOOP（连接后自动订阅）。"
        )
        self._hint.setWordWrap(True)
        self._hint.setStyleSheet("color: palette(mid);")
        layout.addWidget(self._hint)
        return box

    # def _on_map_scale removed for joystick

    def set_motor_count(self, count: int) -> None:
        self._motor_count = max(proto.MOTOR_COUNT_DEFAULT, min(proto.MOTOR_COUNT_MAX, int(count)))
        self._refresh_status()

    def _vector_for_key(self, key: str) -> DriveVector:
        base = DIRECTION_VECTORS[key]
        mag = self._magnitude.value()
        if base.throttle and base.steer:
            scale = _diag(mag)
            return DriveVector(base.throttle * scale, base.steer * scale)
        if base.throttle:
            return DriveVector(base.throttle * mag, 0)
        return DriveVector(0, base.steer * mag)

    def _on_direction_pressed(self, key: str) -> None:
        if not self._drive_enabled():
            return
        self._active_key = key
        vec = self._vector_for_key(key)
        self._worker.request_drive_stream(vec.throttle, vec.steer)
        self._refresh_status(vec)

    def _on_direction_released(self) -> None:
        if self._active_key is None:
            return
        self._active_key = None
        self._worker.request_drive_stream(0, 0)
        self._refresh_status()

    def _stop_drive(self) -> None:
        self._active_key = None
        self._worker.request_drive_stream(0, 0)
        self._refresh_status()

    def _nav_caps_ok(self) -> bool:
        return (
            self._worker.is_connected()
            and bool(self._caps & int(proto.Cap.ANGLE_LOOP))
            and bool(self._caps & int(proto.Cap.DISTANCE_LOOP))
        )

    def _sync_pose_live(self) -> None:
        """用当前编码器/姿态刷新地图位姿（覆盖导航或重规划前调用）。"""
        # self._nav_freeze_odom removed
        if self._last_enc is not None:
            if self._odom.origin_valid:
                self._odom.integrate_encoders(self._last_enc)
            else:
                self._odom.reset_origin(self._last_enc, self._odom.pose.yaw_deg)
        # self._map.set_pose(self._odom.pose)

    def _on_map_target(self, x_mm: float, y_mm: float) -> None:
        if not self._nav_caps_ok():
            self._set_status("需要 ANGLE_LOOP + DISTANCE_LOOP 才能点击导航")
            return
        self._stop_drive()
        if self._navigating:
            self._sync_pose_live()
        plan = self._odom.plan_to_point(x_mm, y_mm)
        if plan is None:
            self._set_status("目标过近，无需移动")
            return
        delta_yaw, dist_mm = plan
        # self._map.set_target(x_mm, y_mm)
        self._navigating = True
        self._nav_id += 1
        if self._last_enc is not None:
            self._odom.sync_encoders(self._last_enc)
        self._nav_freeze_odom = True
        # self._map.set_navigating(True)
        self._update_controls()
        replan = "（覆盖上一目标）" if self._nav_id > 1 else ""
        self._set_status(
            f"导航{replan} → ({x_mm:.0f}, {y_mm:.0f}) mm："
            f"先转 {delta_yaw:+d}° 再走 {dist_mm} mm"
        )
        self._worker.request_navigate(
            delta_yaw,
            dist_mm,
            self._nav_turn_rpm.value(),
            self._nav_drive_rpm.value(),
        )

    def _on_navigation_finished(self, result: NavResult) -> None:
        if result.superseded or result.nav_id != self._nav_id:
            return
        self._navigating = False
        # # self._map.set_navigating(False)
        self._update_controls()
        if not result.command_failed:
            if self._last_enc is not None:
                self._odom.sync_encoders(self._last_enc)
            pose = self._odom.pose
            # self._map.set_pose(pose)
            parts: list[str] = []
            if result.turned and not result.angle_done:
                applied_yaw = result.actual_delta_yaw
                parts.append(f"转约{applied_yaw:+d}°/{result.delta_yaw:+d}°")
            if result.drove and not result.distance_done:
                got = result.final_dist_mm if result.final_dist_mm is not None else 0
                parts.append(f"走约{got:+d}/{result.dist_mm:+d} mm")
            partial = f"（{'，'.join(parts)}）" if parts else ""
            self._set_status(
                f"导航结束 @ ({pose.x_mm:.0f}, {pose.y_mm:.0f}) mm "
                f"yaw {pose.yaw_deg:+.0f}°{partial} — 可点击下一目标"
            )
        else:
            self._set_status("导航指令失败，已停车 — 可重新点击目标")
        # self._nav_freeze_odom removed

    def _calib_yaw(self) -> None:
        if not self._btn_calib_yaw.isEnabled():
            self._set_status("固件不支持 YAW_CALIB")
            return
        self._stop_drive()
        ref_yaw = _normalize_yaw_local(self._calib_yaw_spin.value())
        reply = QMessageBox.question(
            self,
            "校准并重置地图",
            f"请确认车辆已静止放平、电机已停止。\n\n"
            f"将把当前位置设为地图中心，车头朝向设为 {ref_yaw:+d}°。\n\n是否继续？",
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No,
            QMessageBox.StandardButton.No,
        )
        if reply != QMessageBox.StandardButton.Yes:
            return
        self._reset_map_origin(ref_yaw)
        self._worker.request_calib_yaw(ref_yaw)
        self._set_status(f"地图原点已重置，CALIB_YAW ref={ref_yaw:+d}° 已发送")

    def _reset_map_origin(self, yaw_deg: float = 0.0) -> None:
        self._odom.reset_origin(self._last_enc, yaw_deg)
        # self._map.set_pose(self._odom.pose)
        # self._map.set_target(None, None)

    def _drive_enabled(self) -> bool:
        return (
            self._worker.is_connected()
            and bool(self._caps & int(proto.Cap.DRIVE))
           
        )

    def _update_controls(self) -> None:
        drive_on = self._drive_enabled()

        for btn in self._direction_buttons.values():
            btn.setEnabled(drive_on)
        self._magnitude.setEnabled(drive_on)
        
        self._btn_calib_yaw.setEnabled(self._worker.is_connected() and bool(self._caps & int(proto.Cap.YAW_CALIB)))
        self._calib_yaw_spin.setEnabled(self._worker.is_connected() and bool(self._caps & int(proto.Cap.YAW_CALIB)))
        if not drive_on and self._active_key is not None:
            self._active_key = None

    def _refresh_status(self, vec: Optional[DriveVector] = None) -> None:
        if vec is not None:
            motor_text = "两轮" if self._motor_count == proto.MOTOR_COUNT_DEFAULT else "四轮"
            self._set_status(
                f"遥控中 [{motor_text}] throttle={vec.throttle:+d} steer={vec.steer:+d}"
            )
            return
        if self._active_key:
            return
        if not self._worker.is_connected():
            self._set_status("未连接")
        elif not (self._caps & int(proto.Cap.DRIVE)):
            self._set_status("固件不支持 DRIVE 遥控")
        else:
            motor_text = "两轮" if self._motor_count == proto.MOTOR_COUNT_DEFAULT else "四轮"
            self._set_status(f"就绪 [{motor_text}] — 按住方向键或点击地图")

    def _set_status(self, text: str) -> None:
        self._status.setText(text)

    def _on_connected(self, connected: bool) -> None:
        if not connected:
            self.reset()
        else:
            self._update_controls()
            self._refresh_status()

    def on_hello(self, caps: int) -> None:
        self._caps = caps
        self._update_controls()
        self._refresh_status()

    def on_encoder_counts(self, counts): pass

    def on_attitude_yaw(self, yaw_deg): pass

    def reset(self) -> None:
        self._active_key = None
        self._navigating = False
        # self._nav_id removed
        # self._nav_freeze_odom removed
        self._caps = 0
        self._last_enc = None
        # self._odom removed
        # self._map.set_pose(MapPose())
        # self._map.set_target(None, None)
        # # self._map.set_navigating(False)
        if self._worker.is_connected():
            self._worker.request_drive_stream(0, 0)
        self._update_controls()
        self._set_status("未连接")

    def focusOutEvent(self, event: QFocusEvent) -> None:
        new_focus = QApplication.focusWidget()
        if new_focus is not None and self.isAncestorOf(new_focus):
            super().focusOutEvent(event)
            return
        self._stop_drive()
        super().focusOutEvent(event)

    def _on_joystick_direction(self, throttle, steer):
        if not self._drive_enabled():
            return
        self._worker.request_drive_stream(throttle, steer)
        self._set_status("Joystick: throttle={:+d} steer={:+d}".format(throttle, steer))

    def _on_joystick_released(self):
        self._worker.request_drive_stream(0, 0)
        self._refresh_status()
