"""NVS 参数编辑（schema.json 驱动）。"""

from __future__ import annotations

import struct
from typing import Any, Optional

from PySide6.QtWidgets import (
    QFormLayout,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QMessageBox,
    QPushButton,
    QScrollArea,
    QVBoxLayout,
    QWidget,
)

from serial_worker import SerialWorker


LAST_MODE_NAMES = {
    0: "IDLE",
    1: "MANUAL",
    2: "LINE_FOLLOW",
    3: "REMOTE",
}


def decode_c_string(raw: bytes) -> str:
    return raw.split(b"\x00", 1)[0].decode("ascii", errors="replace")


def encode_c_string(text: str, size: int) -> bytes:
    data = text.encode("ascii", errors="replace")[: size - 1]
    return data + b"\x00" * (size - len(data))


def format_last_mode(value: int) -> str:
    name = LAST_MODE_NAMES.get(int(value), "?")
    return f"{int(value)} ({name})"


def expand_struct_field_types(struct_fmt: str) -> list[str]:
    """把 '<6H' / '<fff' 展开为与 fields 一一对应的类型列表 ['H','H',...]/"""
    fmt = struct_fmt.lstrip("<>!@=")
    types: list[str] = []
    i = 0
    while i < len(fmt):
        count = 0
        while i < len(fmt) and fmt[i].isdigit():
            count = count * 10 + int(fmt[i])
            i += 1
        if i >= len(fmt):
            break
        code = fmt[i]
        i += 1
        if count == 0:
            count = 1
        if code == "s":
            types.append(f"{count}s")
        else:
            types.extend([code] * count)
    return types


class ParamEditor(QWidget):
    def __init__(
        self,
        schema_entry: dict[str, Any],
        worker: SerialWorker,
        parent: Optional[QWidget] = None,
    ):
        super().__init__(parent)
        self._entry = schema_entry
        self._worker = worker
        self._fields: dict[str, QLineEdit] = {}

        layout = QVBoxLayout(self)
        title = QLabel(f"{schema_entry['name']} (id={schema_entry['id']})")
        title.setStyleSheet("font-weight: bold;")
        layout.addWidget(title)
        layout.addWidget(QLabel(schema_entry.get("description", "")))

        form = QFormLayout()
        for field_name in schema_entry["fields"]:
            edit = QLineEdit()
            edit.setPlaceholderText(field_name)
            self._fields[field_name] = edit
            form.addRow(field_name, edit)
        layout.addLayout(form)

        btn_row = QHBoxLayout()
        self._read_btn = QPushButton("读取")
        self._write_btn = QPushButton("写入")
        self._write_btn.setEnabled(schema_entry.get("writable", False))
        btn_row.addWidget(self._read_btn)
        btn_row.addWidget(self._write_btn)
        layout.addLayout(btn_row)

        self._read_btn.clicked.connect(self._on_read)
        self._write_btn.clicked.connect(self._on_write)

    @property
    def param_id(self) -> int:
        return int(self._entry["id"])

    @property
    def param_name(self) -> str:
        return str(self._entry["name"])

    def set_values(self, values: tuple[Any, ...]) -> None:
        for name, value in zip(self._entry["fields"], values):
            if name not in self._fields:
                continue
            if isinstance(value, bytes):
                self._fields[name].setText(decode_c_string(value))
            elif isinstance(value, float):
                self._fields[name].setText(f"{value:.6g}")
            else:
                self._fields[name].setText(str(value))

    def read(self) -> None:
        self._worker.request_param_read(self.param_id)

    def write(self) -> None:
        self._on_write()

    def _on_read(self) -> None:
        self.read()

    def _on_write(self) -> None:
        try:
            payload = self._pack_from_form()
        except ValueError as exc:
            QMessageBox.warning(self, "参数错误", str(exc))
            return
        self._worker.request_param_write(self.param_id, payload)

    def _pack_from_form(self) -> bytes:
        entry = self._entry
        fmt_body = entry["struct"].lstrip("<>!@=")

        # 字符串参数：如 "16s"
        if fmt_body.endswith("s") and fmt_body[:-1].isdigit() and len(entry["fields"]) == 1:
            size = int(fmt_body[:-1])
            text = self._fields[entry["fields"][0]].text()
            return encode_c_string(text, size)

        field_types = expand_struct_field_types(entry["struct"])
        if len(field_types) != len(entry["fields"]):
            raise ValueError(
                f"schema 字段数不匹配: struct={entry['struct']} "
                f"→ {len(field_types)} 类型 vs {len(entry['fields'])} 字段"
            )

        values: list[Any] = []
        for field_name, fmt_char in zip(entry["fields"], field_types):
            text = self._fields[field_name].text().strip()
            if fmt_char == "f":
                values.append(float(text))
            elif fmt_char == "B":
                values.append(int(text) & 0xFF)
            elif fmt_char == "H":
                values.append(int(text) & 0xFFFF)
            elif fmt_char in ("h", "i"):
                values.append(int(text))
            elif fmt_char == "I":
                values.append(int(text) & 0xFFFFFFFF)
            else:
                raise ValueError(f"不支持的字段类型: {fmt_char}")
        return struct.pack(entry["struct"], *values)


class ParamPanel(QScrollArea):
    def __init__(self, schema: dict[str, Any], worker: SerialWorker, parent: Optional[QWidget] = None):
        super().__init__(parent)
        self.setWidgetResizable(True)
        self._editors: dict[int, ParamEditor] = {}

        inner = QWidget()
        layout = QVBoxLayout(inner)
        for entry in schema["params"]:
            editor = ParamEditor(entry, worker)
            self._editors[editor.param_id] = editor
            layout.addWidget(editor)
        layout.addStretch()
        self.setWidget(inner)

    def editor(self, param_id: int) -> Optional[ParamEditor]:
        return self._editors.get(param_id)

    def apply_read(self, param_id: int, payload: bytes, schema: dict[str, Any]) -> None:
        entry = next((p for p in schema["params"] if p["id"] == param_id), None)
        editor = self._editors.get(param_id)
        if entry is None or editor is None:
            return
        size = struct.calcsize(entry["struct"])
        raw = payload
        if len(raw) < size:
            # 兼容旧固件 last_mode 按 1 字节 enum 回传
            if entry["name"] == "last_mode" and len(raw) >= 1:
                raw = raw[:1].ljust(size, b"\x00")
            else:
                editor.set_values(tuple("?" for _ in entry["fields"]))
                return
        values = struct.unpack(entry["struct"], raw[:size])
        if entry["struct"].endswith("s"):
            editor.set_values((values[0],))
        elif entry["name"] == "last_mode":
            editor.set_values((format_last_mode(values[0]),))
        else:
            editor.set_values(values)
