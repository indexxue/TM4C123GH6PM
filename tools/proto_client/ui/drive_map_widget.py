# virtual joystick widget

from __future__ import annotations
import math
from typing import Optional
from PySide6.QtCore import QPointF, Qt, Signal
from PySide6.QtGui import QBrush, QColor, QFont, QMouseEvent, QPainter, QPen, QPolygonF
from PySide6.QtWidgets import QSizePolicy, QWidget

def _angle_from_center(dx, dy):
    if abs(dx) < 1e-6 and abs(dy) < 1e-6: return 0.0
    return math.degrees(math.atan2(dx, -dy))

_SECTORS = [
    (-22.5, 22.5,   (1,0)),
    (22.5, 67.5,    (1,1)),
    (67.5, 112.5,   (0,1)),
    (112.5, 157.5,  (-1,1)),
    (157.5, 180.0,  (-1,0)),
    (-180.0,-157.5, (-1,0)),
    (-157.5,-112.5, (-1,-1)),
    (-112.5,-67.5,  (0,-1)),
    (-67.5, -22.5,  (1,-1)),
]
class DriveJoystick(QWidget):

    direction_activated = Signal(int, int)
    direction_released = Signal()

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setMinimumSize(200, 200)
        self.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Expanding)
        self.setMouseTracking(True)
        self._current_angle = None
        self._active = False
        self._magnitude = 500
        self._drag_pos = None

    def set_magnitude(self, value):
        self._magnitude = max(100, min(1000, value))

    def clear(self):
        self._active = False
        self._current_angle = None
        self._drag_pos = None
        self.update()

    def _center(self):
        return QPointF(self.width() * 0.5, self.height() * 0.5)

    def _radius(self):
        return min(self.width(), self.height()) * 0.42

    def _pos_to_angle(self, pos):
        c = self._center()
        dx = pos.x() - c.x()
        dy = pos.y() - c.y()
        if math.hypot(dx, dy) < self._radius() * 0.15:
            return None
        return _angle_from_center(dx, dy)

    def _angle_to_ts(self, angle_deg):
        mag = self._magnitude
        diag = max(1, int(mag * 0.707))
        for lo, hi, (t, s) in _SECTORS:
            if lo <= angle_deg < hi:
                return (t * (diag if t and s else mag), s * (diag if t and s else mag))
        return (0, 0)

    def paintEvent(self, _event):
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing, True)
        rect = self.rect()
        painter.fillRect(rect, QColor(28, 32, 38))
        c = self._center()
        r = self._radius()
        painter.setPen(QPen(QColor(70, 80, 95), 2))
        painter.setBrush(QBrush(QColor(38, 42, 50)))
        painter.drawEllipse(c, r, r)
        painter.setPen(QPen(QColor(55, 62, 72), 1))
        painter.setBrush(Qt.BrushStyle.NoBrush)
        painter.drawEllipse(c, r * 0.5, r * 0.5)
        painter.setPen(QColor(120, 130, 145))
        fn=chr(83)+chr(101)+chr(103)+chr(111)+chr(101)+chr(32)+chr(85)+chr(73)
        painter.setFont(QFont(chr(34)+fn+chr(34), 10))
        painter.drawText(int(c.x()) - 14, int(c.y()) - r * 0.78, chr(21069))
        painter.drawText(int(c.x()) - 14, int(c.y()) + r * 0.82, chr(21518))
        painter.drawText(int(c.x()) - r * 0.82, int(c.y()) + 5, chr(24038))
        painter.drawText(int(c.x()) + r * 0.72, int(c.y()) + 5, chr(21491))
        if self._drag_pos is not None and self._active:
            painter.setPen(Qt.PenStyle.NoPen)
            painter.setBrush(QBrush(QColor(80,180,255,180)))
            painter.drawEllipse(self._drag_pos,6,6)
            dash=QPen(QColor(80,180,255,120))
            dash.setStyle(Qt.PenStyle.DashLine)
            dash.setWidth(1)
            painter.setPen(dash)
            painter.drawLine(c,self._drag_pos)
        painter.setPen(QPen(QColor(40,40,40),1))
        painter.setBrush(QBrush(QColor(255,180,60)))
        angle=self._current_angle if self._current_angle is not None else 0.0
        s=14.0
        rad=math.radians(angle)
        tip=QPointF(c.x()+s*math.sin(rad),c.y()-s*math.cos(rad))
        lx=QPointF(c.x()+s*0.65*math.sin(rad+2.4),c.y()-s*0.65*math.cos(rad+2.4))
        rx=QPointF(c.x()+s*0.65*math.sin(rad-2.4),c.y()-s*0.65*math.cos(rad-2.4))
        painter.drawPolygon(QPolygonF([tip,lx,rx]))
        if self._active and self._current_angle is not None:
            painter.setPen(QColor(140,150,165))
            label=(chr(123)+chr(48)+chr(58)+chr(43)+chr(46)+chr(48)+chr(102)+chr(125)+chr(176)).format(self._current_angle)
            painter.drawText(8,rect.height()-10,label)
    def mousePressEvent(self,event):
        if event.button()==Qt.MouseButton.LeftButton:
            self._handle_drag(event.position())
        super().mousePressEvent(event)

    def mouseMoveEvent(self,event):
        if event.buttons()&Qt.MouseButton.LeftButton:
            self._handle_drag(event.position())
        super().mouseMoveEvent(event)

    def mouseReleaseEvent(self,event):
        if event.button()==Qt.MouseButton.LeftButton and self._active:
            self._active=False
            self._current_angle=None
            self._drag_pos=None
            self.direction_released.emit()
            self.update()
        super().mouseReleaseEvent(event)
    def _handle_drag(self,pos):
        angle=self._pos_to_angle(pos)
        if angle is None:
            if self._active:
                self._active=False
                self._current_angle=None
                self._drag_pos=None
                self.direction_released.emit()
                self.update()
            return
        self._active=True
        self._current_angle=angle
        c=self._center()
        dx=pos.x()-c.x()
        dy=pos.y()-c.y()
        dist=math.hypot(dx,dy)
        r=self._radius()
        if dist>r:
            self._drag_pos=QPointF(c.x()+dx*r/dist,c.y()+dy*r/dist)
        else:
            self._drag_pos=pos
        t,s=self._angle_to_ts(angle)
        self.direction_activated.emit(t,s)
        self.update()