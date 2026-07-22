# TM4C123GH6PM

基于 **TM4C123GH6PM**（Cortex-M4F @ 80 MHz）的 **FreeRTOS** 智能小车固件，含**四轮**与**两轮**两个独立工程。硬件统一 **8 MHz 主晶振** + PLL → 80 MHz。Cursor / VS Code 开发，PowerShell 构建。

仓库：<https://github.com/indexxue/TM4C123GH6PM>

---

## 快速上手

```powershell
git clone git@github.com:indexxue/TM4C123GH6PM.git
cd TM4C123GH6PM
git submodule update --init --recursive

.\build.cmd                 # 首次自动安装 ARM 工具链与 TivaWare SDK
.\flash-jlink.cmd           # J-Link 烧录 standalone（默认四轮）
```

首次编译会把工具链装到 `tools/`、SDK 装到 `sdk/`（均不提交）。若本机无 SDK，从 [TI 官网](https://www.ti.com/tool/SW-TM4C) 下载安装包放到 `downloads/`。完整说明见 **[docs/build.md](docs/build.md)**。

---

## 编译

**WSL（推荐）** — 产品 = `car-4wd` | `car-2wd`：

```bash
cd projects
./build.sh detect
./build.sh car-4wd                       # → projects/car-4wd/build/car-4wd.{elf,hex,bin}
./build.sh car-4wd release 0.1.0         # → release/0.1.0/TM4C123GH6PM_*_unsigned.*
./publish.sh 0.1.0                       # → git tag v0.1.0 + GitHub Release 附件
```

**Windows**（默认 `-CarProject car-4wd`、`-Target standalone`）：

```powershell
.\build.cmd                              # 四轮开发镜像 → projects/car-4wd/build/car-4wd.{elf,hex,bin}
.\build.cmd -CarProject car-2wd          # 两轮开发镜像
.\build.cmd -Target app                  # 量产分区镜像 app.bin
.\build.cmd -Target all                  # Boot + APP_A + 厂测 三镜像
.\build.cmd -Action release -FwVersion 0.1.0
.\publish-release.cmd 0.1.0              # tag + GitHub Release（需 gh auth login）
```

也可在工程目录内编译：

```powershell
.\projects\car-4wd\build.cmd
.\projects\car-2wd\build.cmd
```

IDE：**Ctrl+Shift+B** 默认编译 `car-4wd`。流水线细节见 **[docs/build-pipeline.md](docs/build-pipeline.md)**、**[docs/build.md](docs/build.md)**。

| `-Target` / `IMAGE_TARGET` | 产物（以 car-4wd 为例） | 用途 |
|-----------|-------------------------|------|
| `standalone`（默认） | `build/car-4wd.{elf,hex,bin}` | 日常开发，整片 @ `0x0` |
| `bootloader` | `build/bootloader.*` | Boot @ `0x0` |
| `app` | `build/app.*` | 量产固件 @ `0x4000` |
| `factory` | `build/factory.*` | 厂测 @ `0x21000` |
| `all` | 上述三个镜像 | 产线全套 |

配置源在 `projects/<car>/.syscfg/`；编译前由 `gen_config.py` 生成 `board/`，**勿手改**生成文件。

---

## 下载与烧录

### 前置

1. 先编译，产物在 `projects/<car>/build/`。
2. 调试器：**J-Link** + SWD（推荐）；或 TI **UniFlash**。
3. 目标板上电；J-Link 日志在 `tmp/jlink-flash.log`。
4. J-Link 默认路径见 `scripts/flash-jlink.ps1`（版本不同请改脚本内路径）。

### Standalone（日常开发）

单镜像写到 Flash 起始地址，**无** Boot / 厂测切换：

```powershell
.\build.cmd
.\flash-jlink.cmd                          # car-4wd.bin @ 0x0

.\build.cmd -CarProject car-2wd
.\flash-jlink.cmd -CarProject car-2wd      # car-2wd.bin @ 0x0
```

### 分区模式（量产 + 厂测）

Flash 布局（256 KB）：

```
0x00000000  bootloader.bin   16 KB
0x00004000  app.bin         116 KB   ← 量产（APP_A）
0x00021000  factory.bin     116 KB   ← 厂测（APP_B）
0x0003E000  NVS               8 KB   ← 运行时初始化，无需单独烧
```

产线首次（三镜像）：

```powershell
.\build.cmd -Target all -CarProject car-4wd
.\flash-jlink.cmd -Target bootloader
.\flash-jlink.cmd -Target app
.\flash-jlink.cmd -Target factory
```

日常只更新量产固件（Boot 已在片上）：

```powershell
.\build.cmd -Target app
.\flash-jlink.cmd -Target app
```

| 命令 | 镜像 | 偏移 |
|------|------|------|
| `.\flash-jlink.cmd` | `car-*.bin`（standalone） | `0x0` |
| `.\flash-jlink.cmd -Target bootloader` | `bootloader.bin` | `0x0` |
| `.\flash-jlink.cmd -Target app` | `app.bin` | `0x4000` |
| `.\flash-jlink.cmd -Target factory` | `factory.bin` | `0x21000` |
| `.\flash-jlink.cmd -Target bootloader -EraseAll` | 全片擦除 + 仅 Boot | Boot 自测 |
| `.\flash-jlink.cmd -Target app -EraseApps` | 擦 APP+NVS 后写 app | 保留 Boot |

可选：`-CarProject car-4wd`（默认）或 `car-2wd`。

### UniFlash（可选）

```powershell
.\flash.cmd -Target standalone
```

需本机安装 UniFlash 9.x 与根目录 `TM4C123GH6PM.ccxml`。分区多镜像更建议用 **J-Link**。

### 烧录后验证

- **Standalone**：UART7（PE1，115200）应有 `start:` / `app:` 日志。
- **分区**：复位后 `[boot] jump 0x00004000` → 量产日志；`ftmenter` / OK 长按可进厂测。详见 [PARTITION.md](PARTITION.md)。

---

## 蓝牙上位机（proto_client）

量产 App 启动后拉起蓝牙协议层（`Common/src/proto.c`，115200）。PC 工具在 **`tools/proto_client/`**，协议见 [docs/bluetooth-protocol.md](docs/bluetooth-protocol.md)。

| 接口 | 用途 |
|------|------|
| **UART7**（PE0/PE1） | 开发日志、厂测 `cmd` |
| **蓝牙** | 四轮 **UART0**（PA0/PA1）；两轮 **UART1**（PB0/PB1） |

上位机连**蓝牙虚拟 COM**，勿占用 UART7。

```powershell
cd tools\proto_client
.\run.cmd                    # 安装依赖并启动 GUI
# 或：python -m pip install -r requirements.txt && python main.py
```

顶部连接栏可选 **两轮 / 四轮** 车型（全局共用）；HELLO 会按固件 `hw_rev` 自动识别。

---

## 工程结构

```
bsp_driver/         MCU 外设薄封装
cbb/                芯片驱动子模块（MPU6050、QMC5883P…）
projects/car-4wd/   .syscfg/  board/  main/  build/
projects/car-2wd/   同上（双电机 + 万向轮地面差速）
Common/             公共模块（log、chassis、proto、nvs…）
bootloader/         Boot
factory/            厂测固件
tools/proto_client/ 蓝牙上位机
docs/               设计与构建文档
```

---

## 厂测分区切换

| 项 | 状态 |
|----|------|
| Boot + APP_A + APP_B + NVS slot | ✅ |
| `ftmenter` / `ftmexit`（量产 ↔ 厂测） | ✅ |

详见 [docs/factory-partition-plan.md](docs/factory-partition-plan.md)、[PARTITION.md](PARTITION.md)。

---

## 文档索引

| 文档 | 内容 |
|------|------|
| **[docs/build.md](docs/build.md)** | 编译、构建、烧录、排错（完整说明） |
| [docs/bluetooth-protocol.md](docs/bluetooth-protocol.md) | 蓝牙二进制协议与 PC 工具 |
| [docs/syscfg-io-allocation.md](docs/syscfg-io-allocation.md) | 四轮 / 两轮引脚 |
| [docs/resource-allocation.md](docs/resource-allocation.md) | 定时器 / DMA / 中断 |
| [docs/project-overview.md](docs/project-overview.md) | 项目快照 |
| [docs/car-chassis-reference.md](docs/car-chassis-reference.md) | TivaWare API 参考 |
| [docs/flash-partition.md](docs/flash-partition.md) | Flash 分区 |
| [PARTITION.md](PARTITION.md) | 分区地址速查 |
