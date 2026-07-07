# TM4C123GH6PM

## 项目介绍

基于 **TM4C123GH6PM**（Cortex-M4F @ 80 MHz）的 **FreeRTOS** 智能小车固件，含**四轮**与**两轮**两个独立工程。Cursor / VS Code 开发，PowerShell 构建。

仓库：<https://github.com/indexxue/TM4C123GH6PM>

---

## 快速上手

```powershell
git clone git@github.com:indexxue/TM4C123GH6PM.git
cd TM4C123GH6PM
git submodule update --init --recursive

.\build.cmd                          # 首次构建自动安装工具链与 TivaWare SDK
.\flash-jlink.cmd                    # J-Link 烧录
```

首次编译会自动下载 ARM 工具链到 `tools/`，并将 TivaWare SDK 安装到 `sdk/`（不提交 Git）。若本机无 SDK，需从 [TI 官网](https://www.ti.com/tool/SW-TM4C) 下载安装包放到 `downloads/`。详见 [docs/build.md](docs/build.md)。

编译、多目标构建、代码生成、脚本与排错：**[docs/build.md](docs/build.md)**。

| 常用命令 | 说明 |
|----------|------|
| `.\build.cmd` | 四轮 standalone（`projects/car-4wd/build/car-4wd.bin`） |
| `.\build.cmd -CarProject car-2wd` | 两轮 standalone |
| `.\build.cmd -Target all` | Boot + APP_A + 厂测 |
| `.\projects\car-4wd\build.cmd` | 在工程目录内编译 |

引脚：[docs/syscfg-io-allocation.md](docs/syscfg-io-allocation.md) · 分区：[PARTITION.md](PARTITION.md)

---

## 工程结构

```
bsp_driver/         MCU 外设薄封装
cbb/                芯片驱动子模块
projects/car-4wd/   .syscfg/  board/（设备库）  main/（应用）  build/
projects/car-2wd/   同上
Common/             公共模块（log、start、device_profile、nvs、ota…）
```

---

## 片上 OTA / 厂测（进行中）

| 里程碑 | 状态 |
|--------|------|
| M0 分区与多目标构建 | ✅ |
| M1 Bootloader 跳转 APP_A | ✅ |
| M2 NVS / ota_meta | ✅ |
| M3～M5 OTA 激活与协议 | 待做 |

详见 [docs/ab-ota-dev-plan.md](docs/ab-ota-dev-plan.md)。

---

## 文档索引

| 文档 | 内容 |
|------|------|
| **[docs/build.md](docs/build.md)** | 编译、构建、烧录 |
| [docs/syscfg-io-allocation.md](docs/syscfg-io-allocation.md) | 四轮/两轮引脚 |
| [docs/resource-allocation.md](docs/resource-allocation.md) | 定时器/DMA/中断 |
| [docs/project-overview.md](docs/project-overview.md) | 项目快照 |
| [docs/car-chassis-reference.md](docs/car-chassis-reference.md) | TivaWare API 参考 |
| [docs/flash-partition.md](docs/flash-partition.md) | Flash 分区 |

