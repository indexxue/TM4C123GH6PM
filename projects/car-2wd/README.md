# 两轮小车工程

TM4C123GH6PM 双电机 + 万向轮地面差速固件（无自平衡）。

## 目录

| 路径 | 说明 |
|------|------|
| `.syscfg/` | SysConfig 引脚与外设配置 |
| `board/` | 设备库（`gen_config.py` 生成 motor / encoder / line / board） |
| `main/` | 应用入口（main、app、FreeRTOS） |
| `build/` | 编译产物 |

## 硬件要点

- 驱动轮：**M1 / M2**（硬件 QEI）
- 支撑：**万向轮**（被动，固件按差速底盘处理）
- 蓝牙：**UART1** PB0/PB1 @ 115200（协议层）
- 调试：**UART7** PE0/PE1 @ 115200（日志 / CMD）

## 编译

```powershell
.\build.cmd
# 或仓库根目录：
.\build.cmd -CarProject car-2wd
```

引脚说明见 [docs/syscfg-io-allocation.md](../../docs/syscfg-io-allocation.md) 与 `gpio-allocation.md`。
