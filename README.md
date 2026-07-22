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

**WSL（推荐）** — 产品 = `factory` | `car-4wd` | `car-2wd`：

```bash
cd projects
./build.sh detect
./build.sh car-4wd                       # → projects/car-4wd/build/car-4wd.{elf,hex,bin}
./build.sh car-2wd                       # → projects/car-2wd/build/car-2wd.{elf,hex,bin}
IMAGE_TARGET=factory ./build.sh car-4wd  # 四轮板厂测 → car-4wd/build/factory.*
IMAGE_TARGET=factory ./build.sh car-2wd  # 两轮板厂测 → car-2wd/build/factory.*
./build.sh factory                       # 兼容：factory/.syscfg（4wd 模板）→ factory/build/factory.*
./build.sh car-4wd release 0.1.0         # → bootloader.* + …_unsigned{,_full}.* + …_factory.*
./flash.sh car-4wd                       # J-Link 烧录 standalone
./flash.sh car-4wd full                  # Boot+APP+厂测合并（release / IMAGE_TARGET=all）
./flash.sh car-4wd factory               # 厂测 APP_B（用该车型板级产物）
./publish.sh 0.1.0                       # → git tag v0.1.0 + GitHub Release 附件
```

**Windows**（默认产品 `car-4wd`、`-Target standalone`）：

```powershell
.\build.cmd                              # 四轮开发镜像 → projects/car-4wd/build/car-4wd.{elf,hex,bin}
.\build.cmd car-2wd                      # 两轮开发镜像
.\build.cmd car-4wd -Target factory      # 四轮板厂测 APP_B → car-4wd/build/factory.*
.\build.cmd car-2wd -Target factory      # 两轮板厂测 APP_B → car-2wd/build/factory.*
.\build.cmd factory                      # 兼容：factory/.syscfg（4wd 模板）
.\build.cmd car-4wd -Target app          # 量产分区镜像 app.bin
.\build.cmd car-4wd -Target all          # Boot + APP_A + 厂测 APP_B
.\build.cmd car-4wd -Action release -FwVersion 0.1.0   # 三合一 HEX 打包
.\publish-release.cmd 0.1.0              # tag + GitHub Release（需 gh auth login）
```

也可在工程目录内编译：

```powershell
.\projects\factory\build.cmd
.\projects\car-4wd\build.cmd
.\projects\car-2wd\build.cmd
```

IDE：**Ctrl+Shift+B** 默认编译 `car-4wd`。流水线细节见 **[docs/build-pipeline.md](docs/build-pipeline.md)**、**[docs/build.md](docs/build.md)**。厂测说明见 **[projects/factory/README.md](projects/factory/README.md)**。

| 产品 / `-Target` | 产物 | 用途 |
|-----------|------|------|
| 车型 `-Target factory` | `projects/<car>/build/factory.*` | **推荐**：厂测 APP_B，板级跟该车型 |
| `.\build.cmd factory` | `projects/factory/build/factory.*` | 兼容旧路径（4wd 模板板） |
| 车型 `standalone`（默认） | `projects/<car>/build/<car>.*` | 日常开发，整片 @ `0x0` |
| 车型 `bootloader` | `build/bootloader.*` | Boot @ `0x0` |
| 车型 `app` | `build/app.*` | 量产固件 @ `0x4000` |
| 车型 `all` | boot + app + factory（+ `<car>-full.*`） | 产线全套；`release` 主 HEX 为三合一 |

厂测应用源码共享 `projects/factory/main/`；板级来自构建车型的 `.syscfg/`（`gen_config.py` 生成 `board/`，**勿手改**）。

---

## 下载与烧录

### 前置

1. 先编译，产物在 `projects/<car>/build/`。
2. 调试器：**J-Link** + SWD（推荐）；或 TI **UniFlash**。
3. 目标板上电；J-Link 日志在 `tmp/jlink-flash.log`。
4. J-Link 默认路径见 `scripts/flash-jlink.ps1`（版本不同请改脚本内路径）。

### Standalone（日常开发）

单镜像写到 Flash 起始地址，**无** Boot / 厂测切换：

**WSL：**

```bash
cd projects
./build.sh car-4wd
./flash.sh car-4wd                 # @ 0x0

./build.sh car-2wd
./flash.sh car-2wd
```

**Windows：**

```powershell
.\build.cmd
.\flash-jlink.cmd                          # car-4wd.bin @ 0x0

.\build.cmd car-2wd
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

产线首次（三合一，以四轮为例；两轮把 `car-4wd` 换成 `car-2wd`）：

**WSL：**

```bash
cd projects
./build.sh car-4wd release 0.1.0    # 或 IMAGE_TARGET=all ./build.sh car-4wd
./flash.sh car-4wd full             # 一次烧 Boot+APP+厂测
```

**Windows：**

```powershell
.\build.cmd car-4wd -Action release -FwVersion 0.1.0
.\flash-jlink.cmd -Target full -CarProject car-4wd
```

仍可分段烧录：

```bash
./flash.sh car-4wd bootloader
./flash.sh car-4wd app
./flash.sh car-4wd factory
```

两轮厂测：

```powershell
.\build.cmd car-2wd -Target factory
.\flash-jlink.cmd -Target factory -CarProject car-2wd
```

日常只更新量产固件（Boot 已在片上）：

```bash
# WSL
IMAGE_TARGET=app ./build.sh car-4wd
./flash.sh car-4wd app
```

```powershell
# Windows
.\build.cmd car-4wd -Target app
.\flash-jlink.cmd -Target app
```

| 命令（WSL / Windows） | 镜像 | 偏移 |
|------|------|------|
| `./flash.sh` / `.\flash-jlink.cmd` | `car-*.bin`（standalone） | `0x0` |
| `./flash.sh car-4wd full` / `-Target full` | `<car>-full.hex`（Boot+APP+厂测） | `0x0`..NVS |
| `./flash.sh car-4wd bootloader` | `bootloader.bin` | `0x0` |
| `./flash.sh car-4wd app` | `app.bin` | `0x4000` |
| `./flash.sh car-4wd factory` / `.\flash-jlink.cmd -Target factory -CarProject car-4wd` | `factory.bin`（该车型板） | `0x21000` |
| `./flash.sh car-2wd factory` / `-CarProject car-2wd` | 两轮板厂测 | `0x21000` |
| `./flash.sh factory` / `-Target factory`（默认） | `projects/factory/build/factory.bin` | `0x21000` |
| `./flash.sh car-4wd bootloader --erase-all` | 全片擦除 + 仅 Boot | Boot 自测 |
| `./flash.sh car-4wd app --erase-apps` | 擦 APP+NVS 后写 app | 保留 Boot |

厂测 UART7 命令（`help` / `i2c` / `motor` / `imu` / `enc` / `ftmexit` 等）见 [projects/factory/README.md](projects/factory/README.md)。

### UniFlash（可选）

```powershell
.\flash.cmd -Target standalone
```

需本机安装 UniFlash 9.x 与根目录 `TM4C123GH6PM.ccxml`。分区多镜像更建议用 **J-Link**。

### 烧录后验证

- **Standalone**：UART7（PE1，115200）应有 `start:` / `app:` 日志。
- **分区**：复位后 `[boot] jump 0x00004000` → 量产日志；**OK 长按 10 s** 进厂测；厂测侧 `ftmexit` / OK 长按回量产。详见 [PARTITION.md](PARTITION.md)。

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
projects/factory/   厂测共享 main/（app + serial_cmd）；兼容模板板
projects/car-4wd/   四轮 id=1（.syscfg / board / main / build；可 -Target factory）
projects/car-2wd/   两轮 id=2（双电机 + 万向轮；可 -Target factory）
Common/             公共模块（log、chassis、proto、nvs、device_profile…）
bootloader/         Boot
tools/proto_client/ 蓝牙上位机
docs/               设计与构建文档
```

---

## 厂测分区切换

| 项 | 状态 |
|----|------|
| Boot + APP_A + APP_B + NVS slot | ✅ |
| 量产 OK 长按 → 厂测；厂测 `ftmexit` / OK 长按 → 量产 | ✅ |
| 厂测板级跟车型（`car-X -Target factory`） | ✅ |

详见 [projects/factory/README.md](projects/factory/README.md)、[PARTITION.md](PARTITION.md)、[docs/flash-partition.md](docs/flash-partition.md)。

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
