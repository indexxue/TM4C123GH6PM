# 厂测固件（Factory）

> 链址 **APP_B `0x00021000`**，Boot 按 NVS slot 直接跳转运行。

## 角色

| 项 | 说明 |
|----|------|
| 存储位置 | 片上 Flash APP_B（116 KB） |
| 链接脚本 | `ld/factory.ld` |
| 构建目标 | `.\scripts\build.ps1 -Target factory` |
| 烧录偏移 | `0x00021000` |
| 进入厂测 | 量产侧 `ftmenter` 或 **OK 键长按 10 s** |
| 退出厂测 | `ftmexit` 或 **OK 键长按 10 s** → 回 APP_A |

## 构建与烧录

```powershell
.\scripts\build.ps1 -Target all
.\flash-jlink.cmd -Target bootloader
.\flash-jlink.cmd -Target app
.\flash-jlink.cmd -Target factory
```

## 相关文档

- [PARTITION.md](../PARTITION.md)
- [docs/factory-partition-plan.md](../docs/factory-partition-plan.md)
- [docs/flash-partition.md](../docs/flash-partition.md)
