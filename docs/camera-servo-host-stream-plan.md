# 舵机控制 × 上位机图传接入计划（TM4C 仓库副本）

**版本**：0.2  
**日期**：2026-07-26  
**关联协议**：`docs/camera-spi-protocol.md`（v1.9+）、`docs/bluetooth-protocol.md`  
**文档性质**：产品分工与接口落地记录（与 ESP `camera-servo-host-stream-plan` 对齐）

## 结论

| 通道 | 传什么 | 不传什么 |
|------|--------|----------|
| SPI | 舵机 / 检测 / 云台遥测 / **NET_INFO（IP、端口、path_id）** | JPEG/MJPEG |
| 蓝牙 UART0 | 舵机命令、检测/舵机推送、**转发 cam URL 字段** | 图像 |
| WiFi HTTP | MJPEG/JPEG | 车控主通道 |

## TM4C 已落地

- SPI：`NET_INFO(0x03)` 缓存、`GET_NET_INFO(0x21)`、厂测 `cam net`
- 蓝牙：`0x0040`–`0x0045`、caps `CAMERA`、SUBSCRIBE bit9/10
- 上位机：`tm_proto` / `cli.py` /「相机 / 云台」页

## ESP 需保持同步

实现 `NET_INFO` + `GET_NET_INFO`；HTTP `/api/camera/stream.mjpg`；path_id=0。
