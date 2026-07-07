# 厂测固件（Factory）

> 链址 **APP_B `0x00021000`**，产物 `build/factory.bin` **不直接运行**，须由 Bootloader 搬运至 APP_A 后启动。

## 角色

| 项 | 说明 |
|----|------|
| 存储位置 | 片上 Flash APP_B（116 KB） |
| 链接脚本 | `ld/factory.ld` |
| 构建目标 | `.\scripts\build.ps1 -Target factory` |
| 烧录偏移 | `0x00021000` |
| 激活方式 | 量产固件执行 `ftmenter`（**M6 待实现**，依赖 M3 激活链） |

## 目录

| 文件 | 说明 |
|------|------|
| `factory_main.c` | FreeRTOS 入口 |
| `factory_init.c` | 板级与外设初始化 |
| `factory_app.c` | 厂测任务（灯效、心跳日志） |
| `factory.h` | 公共接口 |

与量产 APP 共用 `Common/`、`projects/<car>/src/startup_*.c`、`freertos_hooks.c` 等；**不**链 `main.c` / `init.c` / `app.c`。

## 构建与烧录

```powershell
# 仅厂测镜像
.\scripts\build.ps1 -Target factory

# 分区全套（Boot + app + factory）
.\scripts\build.ps1 -Target all

# 烧录至 APP_B（产线预置，可选）
.\flash.cmd -Target factory
```

## 相关文档

- [PARTITION.md](../PARTITION.md) — 分区表与烧录速查
- [docs/ab-ota-dev-plan.md](../docs/ab-ota-dev-plan.md) — M6 厂测切换计划
- [docs/flash-partition.md](../docs/flash-partition.md) — 设计细节
