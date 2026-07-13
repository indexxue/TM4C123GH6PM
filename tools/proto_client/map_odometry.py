"""上位机地图坐标系里程计（编码器积分 + 姿态航向）。"""

from __future__ import annotations

import math
from dataclasses import dataclass
from typing import Optional

# 与固件 nvs.c 默认 kinematics 一致
DEFAULT_WHEEL_DIAM_M = 0.065
DEFAULT_GEAR_RATIO = 30.0
DEFAULT_ENCODER_CPR = 11


@dataclass
class Kinematics:
    wheel_diam_m: float = DEFAULT_WHEEL_DIAM_M
    gear_ratio: float = DEFAULT_GEAR_RATIO
    encoder_cpr: int = DEFAULT_ENCODER_CPR

    @property
    def pulses_per_rev(self) -> float:
        return float(self.encoder_cpr) * self.gear_ratio * 4.0

    def counts_to_mm(self, counts: float) -> float:
        ppr = max(1.0, self.pulses_per_rev)
        circ_mm = math.pi * self.wheel_diam_m * 1000.0
        return counts / ppr * circ_mm


@dataclass
class MapPose:
    x_mm: float = 0.0
    y_mm: float = 0.0
    yaw_deg: float = 0.0


def normalize_yaw_deg(deg: float) -> float:
    while deg > 180.0:
        deg -= 360.0
    while deg <= -180.0:
        deg += 360.0
    return deg


def bearing_to_target_deg(dx_mm: float, dy_mm: float) -> float:
    """地图系：+Y 为车头 0°，+X 为右侧 90°。"""
    if abs(dx_mm) < 1e-6 and abs(dy_mm) < 1e-6:
        return 0.0
    return normalize_yaw_deg(math.degrees(math.atan2(dx_mm, dy_mm)))


def delta_yaw_to_target(current_yaw_deg: float, target_bearing_deg: float) -> int:
    err = normalize_yaw_deg(target_bearing_deg - current_yaw_deg)
    return int(round(err))


class MapOdometry:
    def __init__(self, motor_count: int = 2, kinematics: Optional[Kinematics] = None):
        self._motor_count = motor_count
        self._kin = kinematics or Kinematics()
        self._pose = MapPose()
        self._enc_prev: Optional[tuple[int, int, int, int]] = None
        self._origin_valid = False

    @property
    def pose(self) -> MapPose:
        return self._pose

    @property
    def origin_valid(self) -> bool:
        return self._origin_valid

    def set_motor_count(self, motor_count: int) -> None:
        self._motor_count = motor_count

    def set_kinematics(self, kinematics: Kinematics) -> None:
        self._kin = kinematics

    def reset_origin(
        self,
        enc: Optional[tuple[int, int, int, int]] = None,
        yaw_deg: float = 0.0,
    ) -> None:
        self._pose = MapPose(0.0, 0.0, yaw_deg)
        if enc is not None:
            self._enc_prev = enc
            self._origin_valid = True
        else:
            self._enc_prev = None
            self._origin_valid = False

    def set_yaw(self, yaw_deg: float) -> None:
        self._pose.yaw_deg = normalize_yaw_deg(yaw_deg)

    def update_yaw(self, yaw_deg: float) -> None:
        self._pose.yaw_deg = normalize_yaw_deg(yaw_deg)

    def integrate_encoders(self, counts: tuple[int, int, int, int]) -> None:
        if self._enc_prev is None:
            self._enc_prev = counts
            self._origin_valid = True
            return

        d_left = counts[0] - self._enc_prev[0]
        d_right = counts[1] - self._enc_prev[1]
        if self._motor_count >= 4:
            d_left += counts[2] - self._enc_prev[2]
            d_right += counts[3] - self._enc_prev[3]
            d_left //= 2
            d_right //= 2

        self._enc_prev = counts
        avg = (d_left + d_right) * 0.5
        step_mm = self._kin.counts_to_mm(avg)
        if abs(step_mm) < 0.01:
            return

        yaw_rad = math.radians(self._pose.yaw_deg)
        self._pose.x_mm += step_mm * math.sin(yaw_rad)
        self._pose.y_mm += step_mm * math.cos(yaw_rad)

    def plan_to_point(
        self,
        target_x_mm: float,
        target_y_mm: float,
        *,
        min_turn_deg: int = 4,
        min_dist_mm: int = 25,
    ) -> Optional[tuple[int, int]]:
        dx = target_x_mm - self._pose.x_mm
        dy = target_y_mm - self._pose.y_mm
        dist = math.hypot(dx, dy)
        if dist < min_dist_mm:
            return None
        bearing = bearing_to_target_deg(dx, dy)
        delta_yaw = delta_yaw_to_target(self._pose.yaw_deg, bearing)
        dist_mm = int(round(dist))
        if abs(delta_yaw) < min_turn_deg:
            delta_yaw = 0
        return delta_yaw, dist_mm

    def apply_navigation_leg(
        self,
        delta_yaw_deg: int,
        dist_mm: int,
        *,
        final_dist_mm: Optional[int] = None,
    ) -> None:
        """用本次机动结果更新地图位姿（导航期间冻结编码器积分）。"""
        travel_mm = float(final_dist_mm if final_dist_mm is not None else dist_mm)
        if abs(delta_yaw_deg) >= 1:
            self._pose.yaw_deg = normalize_yaw_deg(self._pose.yaw_deg + float(delta_yaw_deg))
        if abs(travel_mm) >= 1.0:
            yaw_rad = math.radians(self._pose.yaw_deg)
            self._pose.x_mm += travel_mm * math.sin(yaw_rad)
            self._pose.y_mm += travel_mm * math.cos(yaw_rad)
