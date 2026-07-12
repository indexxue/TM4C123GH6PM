# proto_client

TM4C123 蓝牙协议上位机（UART0 @ 115200）。协议见 [docs/bluetooth-protocol.md](../../docs/bluetooth-protocol.md)。

## 启动

```powershell
cd tools\proto_client
.\run.cmd
```

需 **Python 3.10+**。依赖：`pyserial`、`PySide6`、`pyqtgraph`。

## 分层结构

与固件 `Common/` + `proto.c` 对应，上位机按职责拆分：

```text
tools/proto_client/
  main.py              # 入口
  main_window.py       # 主窗口壳层（连接栏、Tab、日志）
  tm_proto.py          # 协议编解码（对齐 docs/bluetooth-protocol.md）
  serial_worker.py     # QThread 串口 + 请求/推送（唯一 I/O 线程）
  schema.json          # NVS 参数 schema（PARAM_READ/WRITE UI）
  cli.py               # 无头 CLI（hello/ping/telemetry）
  ui/
    dashboard_tab.py   # 仪表盘
    plot_tab.py        # 姿态 / 循迹 / 编码器 / 转速 + 速度环调试
    speed_panel.py     # SET_SPEED 控制面板（由 plot_tab 引用）
    drive_tab.py       # DRIVE 遥控
    param_panel.py     # schema 驱动参数编辑
```

**约定**

- UI 线程 **不** 直接访问 `serial`；经 `SerialWorker` 槽函数发命令。
- 推送数据经 Qt Signal 分发到各 Tab。
- 协议常量与打包/解析 **仅** 在 `tm_proto.py`，禁止在 UI 硬编码 CMD。

## 功能页

| Tab | 功能 |
|-----|------|
| 仪表盘 | 常驻姿态/编码器 + 按需订阅（电量、循迹等） |
| **姿态 / 循迹 / 速度** | 子页：姿态曲线、循迹条带、编码器曲线、转速曲线 + SET_SPEED |
| 遥控 | DRIVE / DRIVE_STOP |
| 右侧参数 | schema.json 全部 NVS 参数读写 |

连接成功后自动 SUBSCRIBE：姿态 + 编码器；若固件 caps 含 `SPEED_LOOP` 则**自动**订阅 **MOTOR_RPM** 推送。

「姿态 / 循迹 / 速度」→「转速 / 调试」页：M1~M4 独立曲线（可勾选显示/隐藏），支持 **两轮 / 四轮** 车型切换（HELLO `hw_rev` 自动识别）。

## CLI

```powershell
python cli.py COM7 hello
python cli.py COM7 telemetry
python cli.py COM7 param-read --param-id 5
```
