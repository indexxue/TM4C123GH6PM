"""
PySide6 主窗口壳层：连接栏 + 左侧遥测/监控/遥控 + 右侧参数面板。

分层约定见 tools/proto_client/README.md
"""

from __future__ import annotations

import sys
import time
from typing import Optional

import pyqtgraph as pg
import tm_proto as proto
from PySide6.QtCore import Qt, QTimer
from PySide6.QtWidgets import (
    QApplication,
    QComboBox,
    QGroupBox,
    QHBoxLayout,
    QLabel,
    QMainWindow,
    QMessageBox,
    QPlainTextEdit,
    QPushButton,
    QSplitter,
    QTabWidget,
    QVBoxLayout,
    QWidget,
)

from serial_worker import SerialWorker, list_serial_ports, load_schema
from ui.dashboard_tab import DashboardTab
from ui.drive_tab import DriveTab
from ui.param_panel import ParamPanel
from ui.pid_tuning_tab import PidTuningTab
from ui.plot_tab import PlotTab, deg_to_180

DEFAULT_SERIAL_PORT = "COM26"


class MainWindow(QMainWindow):
    def __init__(self) -> None:
        super().__init__()
        self.setWindowTitle("TM4C123 蓝牙协议工具")
        self.resize(1280, 840)

        self._schema = load_schema()
        self._proto_ver = proto.PROTO_VER

        self._worker = SerialWorker()
        self._worker.log.connect(self._append_log)
        self._worker.connected_changed.connect(self._on_connected)
        self._worker.hello_received.connect(self._on_hello)
        self._worker.link_alive_changed.connect(self._on_link_alive)
        self._worker.push_received.connect(self._on_push)
        self._worker.motor_rpm_received.connect(self._on_motor_rpm)
        self._worker.angle_loop_received.connect(self._on_angle_loop)
        self._worker.distance_loop_received.connect(self._on_distance_loop)
        self._worker.encoder_counts_received.connect(self._on_encoder_counts)
        self._worker.battery_received.connect(self._on_battery)
        self._worker.optional_subscription_changed.connect(self._on_optional_subscription)
        self._worker.param_read_result.connect(self._on_param_read)
        self._worker.param_write_result.connect(self._on_param_write)

        central = QWidget()
        self.setCentralWidget(central)
        root = QVBoxLayout(central)
        root.addWidget(self._build_connection_bar())

        self._param_panel = ParamPanel(self._schema, self._worker)

        splitter = QSplitter(Qt.Horizontal)
        splitter.addWidget(self._build_left_tabs())
        splitter.addWidget(self._param_panel)
        splitter.setStretchFactor(0, 3)
        splitter.setStretchFactor(1, 2)
        root.addWidget(splitter, stretch=1)

        log_box = QGroupBox("日志")
        log_layout = QVBoxLayout(log_box)
        self._log_view = QPlainTextEdit()
        self._log_view.setReadOnly(True)
        self._log_view.setMaximumBlockCount(500)
        self._log_view.setMinimumHeight(100)
        log_layout.addWidget(self._log_view)
        root.addWidget(log_box)

        self._port_timer = QTimer(self)
        self._port_timer.timeout.connect(self._refresh_ports)
        self._port_timer.start(2000)
        self._refresh_ports()

        self._worker.start()

    def closeEvent(self, event) -> None:  # noqa: N802
        self._worker.stop()
        super().closeEvent(event)

    def _build_connection_bar(self) -> QGroupBox:
        box = QGroupBox("连接")
        layout = QHBoxLayout(box)

        self._port_combo = QComboBox()
        self._baud_combo = QComboBox()
        self._baud_combo.setEditable(True)
        for rate in (9600, 19200, 38400, 57600, 115200, 230400):
            self._baud_combo.addItem(str(rate), rate)
        self._baud_combo.setCurrentText("115200")

        self._connect_btn = QPushButton("连接")
        self._disconnect_btn = QPushButton("断开")
        self._disconnect_btn.setEnabled(False)
        self._link_label = QLabel("未连接")
        self._hello_label = QLabel("—")
        self._hello_label.setWordWrap(True)

        self._connect_btn.clicked.connect(self._on_connect)
        self._disconnect_btn.clicked.connect(self._on_disconnect)

        layout.addWidget(QLabel("串口"))
        layout.addWidget(self._port_combo, stretch=1)
        layout.addWidget(QLabel("波特率"))
        layout.addWidget(self._baud_combo)
        layout.addWidget(self._connect_btn)
        layout.addWidget(self._disconnect_btn)
        layout.addWidget(self._link_label)
        layout.addWidget(self._hello_label, stretch=2)
        return box

    def _build_left_tabs(self) -> QTabWidget:
        tabs = QTabWidget()
        self._tabs = tabs
        self._dashboard = DashboardTab()
        self._plot = PlotTab(
            self._worker,
            self._param_panel.editor(5),
            self._param_panel.editor(7),
            self._param_panel.editor(16),
            self._param_panel.editor(18),
        )
        self._drive = DriveTab(self._worker)
        self._pid_tuning = PidTuningTab(self._worker)

        self._dashboard.subscribe_panel.changed.connect(self._on_subscribe_apply)

        tabs.addTab(self._dashboard, "仪表盘")
        tabs.addTab(self._plot, "姿态 / 循迹 / 速度")
        tabs.addTab(self._pid_tuning, "PID 整定")
        tabs.addTab(self._drive, "遥控")
        return tabs

    def _on_subscribe_apply(self, optional_mask: int) -> None:
        if self._disconnect_btn.isEnabled():
            self._worker.request_apply_subscription(optional_mask)

    def _refresh_ports(self) -> None:
        current = self._port_combo.currentText()
        ports = list_serial_ports()
        if DEFAULT_SERIAL_PORT not in ports:
            ports.insert(0, DEFAULT_SERIAL_PORT)
        self._port_combo.blockSignals(True)
        self._port_combo.clear()
        self._port_combo.addItems(ports)
        pick = current or DEFAULT_SERIAL_PORT
        idx = self._port_combo.findText(pick)
        if idx >= 0:
            self._port_combo.setCurrentIndex(idx)
        self._port_combo.blockSignals(False)

    def _on_connect(self) -> None:
        port = self._port_combo.currentText()
        if not port:
            QMessageBox.warning(self, "连接", "请选择串口")
            return
        try:
            baudrate = int(self._baud_combo.currentText().strip())
        except ValueError:
            QMessageBox.warning(self, "连接", "波特率无效")
            return
        if baudrate <= 0:
            QMessageBox.warning(self, "连接", "波特率无效")
            return
        self._worker.connect_port(port, baudrate)

    def _on_disconnect(self) -> None:
        self._worker.disconnect_port()

    def _on_connected(self, connected: bool) -> None:
        self._connect_btn.setEnabled(not connected)
        self._disconnect_btn.setEnabled(connected)
        self._port_combo.setEnabled(not connected)
        self._baud_combo.setEnabled(not connected)
        if not connected:
            self._link_label.setText("未连接")
            self._hello_label.setText("—")
            self._dashboard.reset()
            self._plot.reset()
            self._pid_tuning.reset()

    def _on_hello(self, info: proto.HelloInfo) -> None:
        self._proto_ver = info.proto_ver
        self._hello_label.setText(
            f"proto={info.proto_ver}  fw={info.fw_version}  hw_rev={info.hw_rev}  "
            f"caps={proto.caps_text(info.caps)}"
        )
        has_speed = bool(info.caps & int(proto.Cap.SPEED_LOOP))
        has_angle = bool(info.caps & int(proto.Cap.ANGLE_LOOP))
        has_distance = bool(info.caps & int(proto.Cap.DISTANCE_LOOP))
        self._dashboard.subscribe_panel.set_rpm_available(has_speed)
        self._dashboard.subscribe_panel.set_angle_available(has_angle)
        self._dashboard.subscribe_panel.set_distance_available(has_distance)
        if info.hw_rev == proto.HW_REV_CAR_4WD_V1:
            self._append_log("提示: 固件为四轮车型，上位机默认按两轮调试；可在各页切换「四轮」")
        self._pid_tuning.on_hello(info.caps)
        if info.proto_ver < proto.PROTO_VER:
            self._append_log(f"提示: 固件 proto_ver={info.proto_ver}，姿态按 v1 f32 解析")

    def _on_optional_subscription(self, mask: int) -> None:
        self._dashboard.set_optional_channels(mask)
        self._dashboard.subscribe_panel.apply_mask(mask)
        self._plot.set_rpm_subscribed(bool(mask & int(proto.TelChannel.MOTOR_RPM)))
        self._plot.set_angle_subscribed(bool(mask & int(proto.TelChannel.ANGLE_LOOP)))
        self._plot.set_distance_subscribed(bool(mask & int(proto.TelChannel.DISTANCE_LOOP)))

    def _on_link_alive(self, alive: bool) -> None:
        if alive:
            self._link_label.setText("在线")
            self._link_label.setStyleSheet("color: green; font-weight: bold;")
        elif self._disconnect_btn.isEnabled():
            self._link_label.setText("重连中")
            self._link_label.setStyleSheet("color: #c8860a; font-weight: bold;")
        else:
            self._link_label.setText("断线")
            self._link_label.setStyleSheet("color: red; font-weight: bold;")

    @staticmethod
    def _deg_to_180(deg: float) -> float:
        return deg_to_180(deg)

    def _on_push(self, push: proto.TelemetryPush) -> None:
        self._dashboard.update_uptime(push.uptime_ms)
        try:
            if push.channel_id == proto.CHANNEL_ID_ATTITUDE:
                roll, pitch, yaw = proto.parse_attitude_push(push.payload, self._proto_ver)
                r, p, y = (int(round(self._deg_to_180(v))) for v in (roll, pitch, yaw))
                self._dashboard.update_attitude_text(r, p, y)
                self._plot.update_attitude(roll, pitch, yaw)
            elif push.channel_id == proto.CHANNEL_ID_LINE_ADC:
                values = proto.parse_line_adc_push(push.payload)
                self._dashboard.update_line_adc(values)
                self._plot.update_line_adc(values)
            elif push.channel_id == proto.CHANNEL_ID_ULTRASONIC:
                self._dashboard.update_ultrasonic(proto.parse_ultrasonic_push(push.payload))
        except proto.ProtoError as exc:
            self._append_log(f"推送解析: {exc}")

    def _on_battery(self, batt_mv: int, batt_pct: int) -> None:
        self._dashboard.update_battery(batt_mv, batt_pct)

    def _on_motor_rpm(self, rpms: tuple[int, int, int, int]) -> None:
        self._plot.on_motor_rpm(rpms)
        self._pid_tuning.on_motor_rpm(rpms)

    def _on_angle_loop(self, sample: proto.AngleLoopPush) -> None:
        self._plot.on_angle_loop(sample)
        self._pid_tuning.on_angle_loop(sample)

    def _on_distance_loop(self, sample: proto.DistanceLoopPush) -> None:
        self._plot.on_distance_loop(sample)
        self._pid_tuning.on_distance_loop(sample)

    def _on_encoder_counts(self, counts: tuple[int, int, int, int]) -> None:
        self._dashboard.update_encoder_counts(counts)
        self._plot.on_encoder_counts(counts)

    def _on_param_read(self, param_id: int, payload: bytes) -> None:
        self._param_panel.apply_read(param_id, payload, self._schema)
        self._pid_tuning.apply_param_read(param_id, payload)

    def _on_param_write(self, param_id: int, ok: bool, message: str) -> None:
        editor = self._param_panel.editor(param_id)
        label = editor.param_name if editor else str(param_id)
        if ok:
            self._append_log(f"PARAM_WRITE {label} 成功")
        else:
            QMessageBox.warning(self, "写入失败", f"{label}: {message}")

    def _append_log(self, text: str) -> None:
        stamp = time.strftime("%H:%M:%S")
        self._log_view.appendPlainText(f"[{stamp}] {text}")
        scrollbar = self._log_view.verticalScrollBar()
        scrollbar.setValue(scrollbar.maximum())


def main() -> int:
    app = QApplication(sys.argv)
    pg.setConfigOptions(antialias=True)
    window = MainWindow()
    window.show()
    return app.exec()


if __name__ == "__main__":
    raise SystemExit(main())
