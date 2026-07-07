# TM4C123 嵌入式编码规范（依赖分层）

与 ESP32-S3 参考仓库对齐的依赖方向；新增模块前请先确认所属层级。

## 分层

```
projects/<car>/main   →  common, bsp_driver, board（full profile）
common                →  bsp_driver, board（full profile）, cbb
board/（syscfg 生成）  →  bsp_driver / TivaWare DriverLib
cbb/                  →  bsp_driver 或 TivaWare（不依赖 common）
bsp_driver/           →  TivaWare DriverLib only
```

## 目录

| 路径 | 职责 |
|------|------|
| `bsp_driver/` | MCU 外设薄封装（uart、gpio、i2c…） |
| `cbb/` | 芯片协议驱动（ws2812b、mpu6050、tb6612…），回调注入 |
| `Common/` | 跨产品服务：log、nvs、cmd、ota、device_profile、start |
| `projects/<car>/board/` | syscfg 生成的 motor/encoder/line/board |
| `projects/<car>/main/` | 薄应用：main.c、app.c、FreeRTOS 钩子 |

## device_profile

- 编译期 `-DDEVICE_PRODUCT_ID=...` 选择产品档案
- `board_mask` 控制 Motor/Encoder/Line/Board_Periph 初始化
- `platform_mask` 控制 log/cmd/ota/led/button 等平台服务
- v1 默认：`car-4wd` + `log-only`（仅 UART7 日志心跳）

## 构建

```powershell
.\build.cmd                              # car-4wd log-only（默认）
.\build.cmd -Profile full                # 全外设
.\build.cmd -CarProject car-2wd -Profile full
.\flash-jlink.cmd
```

## 禁止

- 在 `Common/inc/` 放置 syscfg 生成的 board 头文件
- `cbb` 或 `bsp_driver` 引用 `common` 头文件
- 手改 `projects/*/board/src/{motor,encoder,line,board}.c`
