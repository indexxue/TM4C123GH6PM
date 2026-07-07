# 四轮小车工程

TM4C123GH6PM 四电机底盘固件。

## 目录

| 路径 | 说明 |
|------|------|
| `.syscfg/` | SysConfig 引脚与外设配置（`tm4c123gh6pm.syscfg`） |
| `source/` | 设备库（`gen_config.py` 生成 motor / encoder / line / board） |
| `src/` | 应用入口（main、init、app、FreeRTOS） |
| `build/` | 编译产物 |

## 编译

```powershell
.\build.cmd
# 或仓库根目录：
.\build.cmd -CarProject car-4wd
```

引脚说明见 [docs/syscfg-io-allocation.md](../../docs/syscfg-io-allocation.md) 四电机章节。
