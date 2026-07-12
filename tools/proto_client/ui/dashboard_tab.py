"""仪表盘：电池 / 姿态 / 编码器 / 循迹 / 超声波 + 按需订阅。"""

from __future__ import annotations

from typing import Optional

import tm_proto as proto
from PySide6.QtWidgets import QFormLayout, QLabel, QVBoxLayout, QWidget

from ui.subscribe_panel import SubscribePanel


class DashboardTab(QWidget):
    ULTRASONIC_DISCONNECT_STREAK = 10

    def __init__(self, parent: Optional[QWidget] = None):
        super().__init__(parent)
        self._ultra_invalid_streak = 0
        self._ultra_last_valid_mm: Optional[int] = None
        self._batt_subscribed = False
        self._line_subscribed = False
        self._ultra_subscribed = False

        root = QVBoxLayout(self)
        self._subscribe = SubscribePanel()
        root.addWidget(self._subscribe)

        layout = QFormLayout()
        self.lbl_batt = QLabel("—（未订阅）")
        self.lbl_att = QLabel("—")
        self.lbl_enc = QLabel("—")
        self.lbl_line = QLabel("—（未订阅）")
        self.lbl_ultra = QLabel("—（未订阅）")
        self.lbl_uptime = QLabel("—")
        layout.addRow("电池", self.lbl_batt)
        layout.addRow("姿态 (°)", self.lbl_att)
        layout.addRow("编码器", self.lbl_enc)
        layout.addRow("循迹 0/1", self.lbl_line)
        layout.addRow("超声波", self.lbl_ultra)
        layout.addRow("Uptime", self.lbl_uptime)
        root.addLayout(layout)

    @property
    def subscribe_panel(self) -> SubscribePanel:
        return self._subscribe

    def reset(self) -> None:
        self._ultra_invalid_streak = 0
        self._ultra_last_valid_mm = None
        self._batt_subscribed = False
        self._line_subscribed = False
        self._ultra_subscribed = False
        self._subscribe.reset()
        for lbl in (
            self.lbl_batt,
            self.lbl_att,
            self.lbl_enc,
            self.lbl_line,
            self.lbl_ultra,
            self.lbl_uptime,
        ):
            lbl.setText("—")
        self.lbl_batt.setText("—（未订阅）")
        self.lbl_line.setText("—（未订阅）")
        self.lbl_ultra.setText("—（未订阅）")

    def set_optional_channels(self, mask: int) -> None:
        self._batt_subscribed = bool(mask & int(proto.TelChannel.BATT))
        self._line_subscribed = bool(mask & int(proto.TelChannel.LINE_ADC))
        self._ultra_subscribed = bool(mask & int(proto.TelChannel.ULTRASONIC))
        if not self._batt_subscribed:
            self.lbl_batt.setText("—（未订阅）")
        if not self._line_subscribed:
            self.lbl_line.setText("—（未订阅）")
        if not self._ultra_subscribed:
            self.lbl_ultra.setText("—（未订阅）")

    def update_encoder_counts(self, counts: tuple[int, int, int, int]) -> None:
        self.lbl_enc.setText(
            f"M1={counts[0]}  M2={counts[1]}  M3={counts[2]}  M4={counts[3]}"
        )

    def update_battery(self, batt_mv: int, batt_pct: int) -> None:
        if self._batt_subscribed:
            self.lbl_batt.setText(f"{batt_mv} mV  ({batt_pct}%)")

    def update_attitude_text(self, roll: int, pitch: int, yaw: int) -> None:
        self.lbl_att.setText(f"roll={roll}  pitch={pitch}  yaw={yaw}")

    def update_line_adc(self, values: tuple[int, ...]) -> None:
        if self._line_subscribed:
            self.lbl_line.setText("  ".join(str(v) for v in values))

    def update_uptime(self, uptime_ms: int) -> None:
        self.lbl_uptime.setText(f"{uptime_ms} ms")

    def update_ultrasonic(self, distance_mm: int) -> None:
        if not self._ultra_subscribed:
            return
        if distance_mm < proto.ULTRASONIC_INVALID_MM:
            self._ultra_invalid_streak = 0
            self._ultra_last_valid_mm = distance_mm
            self.lbl_ultra.setText(proto.format_ultrasonic_mm(distance_mm))
            return

        self._ultra_invalid_streak += 1
        if self._ultra_invalid_streak < self.ULTRASONIC_DISCONNECT_STREAK:
            if self._ultra_last_valid_mm is not None:
                self.lbl_ultra.setText(proto.format_ultrasonic_mm(self._ultra_last_valid_mm))
            return

        self._ultra_last_valid_mm = None
        self.lbl_ultra.setText("未连接")
