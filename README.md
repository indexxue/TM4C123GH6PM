# TM4C123GH6PM

## 项目介绍

基于 **TM4C123GH6PM**（Cortex-M4F @ 80 MHz）的 **FreeRTOS** 四轴小车固件。Cursor / VS Code 开发，PowerShell 构建。

仓库：<https://github.com/indexxue/TM4C123GH6PM>

---

## 快速上手

**前提**：安装 [TivaWare C Series 2.2.0.295](https://www.ti.com/tool/SW-TM4C)（默认 `D:\Ti\TivaWare_C_Series-2.2.0.295`）。

```powershell
git clone git@github.com:indexxue/TM4C123GH6PM.git
cd TM4C123GH6PM

.\build.cmd          # 编译
.\flash.cmd          # 烧录（需 UniFlash）
```

编译、多目标构建、代码生成、脚本与排错：**[docs/build.md](docs/build.md)**（编译相关文档的单一来源）。

| 常用命令 | 说明 |
|----------|------|
| `.\build.cmd` | standalone 开发镜像 |
| `.\build.cmd -Target all` | Boot + APP_A + 厂测 |
| `.\flash.cmd -Target prod` | 首次量产分区烧录 |

引脚表：[docs/gpio-allocation.md](docs/gpio-allocation.md) · 分区：[PARTITION.md](PARTITION.md)

---

## 片上 OTA / 厂测（进行中）

| 里程碑 | 状态 |
|--------|------|
| M0 分区与多目标构建 | ✅ |
| M1 Bootloader 跳转 APP_A | ✅ |
| M2 NVS / ota_meta | ✅ |
| M3～M5 OTA 激活与协议 | 待做 |
| M6 厂测切换 | 部分（`factory/` 已就绪） |

详见 [docs/ab-ota-dev-plan.md](docs/ab-ota-dev-plan.md)。

---

## 推送

```powershell
git add .
git commit -m "描述本次改动"
git push origin main
```

---

## 文档索引

| 文档 | 内容 |
|------|------|
| **[docs/build.md](docs/build.md)** | **编译、构建、烧录、脚本** |
| [docs/gpio-allocation.md](docs/gpio-allocation.md) | GPIO 引脚分配 |
| [docs/flash-partition.md](docs/flash-partition.md) | Flash 分区设计 |
| [docs/project-overview.md](docs/project-overview.md) | 项目总览 |
| [docs/car-chassis-reference.md](docs/car-chassis-reference.md) | 底盘开发参考 |
