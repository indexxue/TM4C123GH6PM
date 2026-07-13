"""遥控页地图：显示位姿、目标点，左键点击导航。"""

from __future__ import annotations

import math
from typing import Optional

from PySide6.QtCore import QPointF, Qt, Signal
from PySide6.QtGui import QBrush, QColor, QFont, QMouseEvent, QPainter, QPen, QPolygonF
from PySide6.QtWidgets import QSizePolicy, QWidget

from map_odometry import MapPose


class DriveMapWidget(QWidget):
    target_clicked = Signal(float, float)

    def __init__(self, parent: Optional[QWidget] = None):
        super().__init__(parent)
        self.setMinimumSize(360, 360)
        self.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Expanding)
        self.setMouseTracking(True)
        self._mm_per_px = 4.0
        self._pose = MapPose()
        self._target: Optional[tuple[float, float]] = None
        self._navigating = False

    def set_scale_mm_per_px(self, value: float) -> None:
        self._mm_per_px = max(0.5, float(value))
        self.update()

    def set_pose(self, pose: MapPose) -> None:
        self._pose = pose
        self.update()

    def set_target(self, x_mm: Optional[float], y_mm: Optional[float]) -> None:
        if x_mm is None or y_mm is None:
            self._target = None
        else:
            self._target = (x_mm, y_mm)
        self.update()

    def set_navigating(self, active: bool) -> None:
        self._navigating = active
        self.update()

    def target_mm(self) -> Optional[tuple[float, float]]:
        return self._target

    def _widget_center(self) -> QPointF:
        return QPointF(self.width() * 0.5, self.height() * 0.5)

    def _world_to_widget(self, x_mm: float, y_mm: float) -> QPointF:
        c = self._widget_center()
        return QPointF(c.x() + x_mm / self._mm_per_px, c.y() - y_mm / self._mm_per_px)

    def _widget_to_world(self, px: float, py: float) -> tuple[float, float]:
        c = self._widget_center()
        x_mm = (px - c.x()) * self._mm_per_px
        y_mm = (c.y() - py) * self._mm_per_px
        return x_mm, y_mm

    def paintEvent(self, _event) -> None:
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing, True)
        rect = self.rect()
        painter.fillRect(rect, QColor(28, 32, 38))

        center = self._widget_center()
        grid_pen = QPen(QColor(55, 62, 72))
        grid_pen.setWidth(1)
        painter.setPen(grid_pen)
        step_px = max(20.0, 500.0 / self._mm_per_px)
        x = center.x()
        while x < rect.width():
            painter.drawLine(int(x), 0, int(x), rect.height())
            x += step_px
        x = center.x() - step_px
        while x > 0:
            painter.drawLine(int(x), 0, int(x), rect.height())
            x -= step_px
        y = center.y()
        while y < rect.height():
            painter.drawLine(0, int(y), rect.width(), int(y))
            y += step_px
        y = center.y() - step_px
        while y > 0:
            painter.drawLine(0, int(y), rect.width(), int(y))
            y -= step_px

        axis_pen = QPen(QColor(90, 100, 115))
        axis_pen.setWidth(2)
        painter.setPen(axis_pen)
        painter.drawLine(int(center.x()), 0, int(center.x()), rect.height())
        painter.drawLine(0, int(center.y()), rect.width(), int(center.y()))

        painter.setPen(QColor(140, 150, 165))
        painter.setFont(QFont("Segoe UI", 9))
        painter.drawText(int(center.x()) + 6, 16, "+Y 前")
        painter.drawText(rect.width() - 36, int(center.y()) - 6, "+X")

        if self._target is not None:
            tx, ty = self._target
            tp = self._world_to_widget(tx, ty)
            cp = self._world_to_widget(self._pose.x_mm, self._pose.y_mm)
            dash = QPen(QColor(80, 180, 255, 180))
            dash.setStyle(Qt.PenStyle.DashLine)
            dash.setWidth(2)
            painter.setPen(dash)
            painter.drawLine(cp, tp)

            painter.setPen(Qt.PenStyle.NoPen)
            painter.setBrush(QBrush(QColor(80, 180, 255, 200)))
            painter.drawEllipse(tp, 8, 8)
            painter.setPen(QColor(200, 230, 255))
            painter.drawText(int(tp.x()) + 10, int(tp.y()) + 4, "目标")

        car_pt = self._world_to_widget(self._pose.x_mm, self._pose.y_mm)
        self._draw_car(painter, car_pt, self._pose.yaw_deg)

        if self._navigating:
            painter.setPen(QColor(255, 200, 80))
            painter.drawText(8, rect.height() - 10, "导航中…")

        painter.setPen(QColor(120, 130, 145))
        painter.drawText(
            8,
            18,
            f"({self._pose.x_mm:.0f}, {self._pose.y_mm:.0f}) mm  yaw {self._pose.yaw_deg:+.0f}°",
        )

    def _draw_car(self, painter: QPainter, center: QPointF, yaw_deg: float) -> None:
        size = 14.0
        rad = math.radians(yaw_deg)
        tip = QPointF(
            center.x() + size * math.sin(rad),
            center.y() - size * math.cos(rad),
        )
        left = QPointF(
            center.x() + size * 0.65 * math.sin(rad + 2.4),
            center.y() - size * 0.65 * math.cos(rad + 2.4),
        )
        right = QPointF(
            center.x() + size * 0.65 * math.sin(rad - 2.4),
            center.y() - size * 0.65 * math.cos(rad - 2.4),
        )
        poly = QPolygonF([tip, left, right])
        painter.setPen(QPen(QColor(40, 40, 40), 1))
        painter.setBrush(QBrush(QColor(255, 180, 60)))
        painter.drawPolygon(poly)

    def mousePressEvent(self, event: QMouseEvent) -> None:
        if event.button() == Qt.MouseButton.LeftButton:
            x_mm, y_mm = self._widget_to_world(event.position().x(), event.position().y())
            self.target_clicked.emit(x_mm, y_mm)
        super().mousePressEvent(event)
