# TM4C123GH6PM

## 项目介绍

基于 **TM4C123GH6PM**（Cortex-M4F @ 80 MHz）的 **FreeRTOS** 智能小车固件，含**四轮**与**两轮**两个独立工程。硬件统一 **8 MHz 主晶振** + PLL。Cursor / VS Code 开发，PowerShell 构建。

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
| `.\build.cmd -Target all` | Boot + APP_A + 厂测（见下方 **烧录**） |
| `.\flash-jlink.cmd -Target app` | 仅烧量产固件 @ APP_A（须已有 Boot） |
| `.\projects\car-4wd\build.cmd` | 在工程目录内编译 |

引脚：[docs/syscfg-io-allocation.md](docs/syscfg-io-allocation.md) · 分区：[PARTITION.md](PARTITION.md)

---

## 蓝牙上位机（proto_client）

固件量产 App 启动后会拉起 **UART0 蓝牙协议层**（`Common/src/proto.c`，115200 @ PA0/PA1）。PC 侧工具在 **`tools/proto_client/`**（与 ARM 工具链同目录，本地存在；协议细节见 [docs/bluetooth-protocol.md](docs/bluetooth-protocol.md)）。

### 串口分工

| 接口 | 用途 |
|------|------|
| **UART7**（PE1，115200） | 开发日志、厂测 `cmd` 命令行 |
| **UART0 / 蓝牙 HC-05**（115200） | 二进制协议：遥测、参数、遥控 |

上位机请连接 **蓝牙虚拟 COM**，不要占用 UART7。

### 安装依赖

需 **Python 3.10+** 与 pip：

```powershell
cd tools\proto_client
python -m pip install -r requirements.txt
```

依赖：`pyserial`、`PySide6`、`pyqtgraph`。

### 启动 GUI

```powershell
cd tools\proto_client
                 # 安装依赖并启动主界面
```

---

## 烧录

### 前置条件

1. **先编译**：产物在 `projects/<car>/build/`（默认 `car-4wd`）。
2. **调试器**：Segger **J-Link** + SWD（脚本默认路径 `C:\Program Files\SEGGER\JLink_V818\JLink.exe`，版本不同请改 `scripts/flash-jlink.ps1`）。
3. **连接**：SWD 接好、目标板上电；烧录 log 在 `tmp/jlink-flash.log`。

### 两种模式

| 模式 | 构建 | 适用 |
|------|------|------|
| **Standalone** | `.\build.cmd` | 日常开发，单镜像 @ `0x0`，**无** Boot / 厂测切换 |
| **分区（Boot + APP_A + APP_B）** | `.\build.cmd -Target all` | 量产 + 厂测；Boot 按 NVS slot 跳转 |

分区布局（256 KB Flash）：

```
0x00000000  bootloader.bin   16 KB
0x00004000  app.bin         116 KB   ← 量产生效固件（APP_A）
0x00021000  factory.bin     116 KB   ← 厂测镜像（APP_B，产线预烧）
0x0003E000  NVS               8 KB   ← 运行时初始化，无需单独烧录
```

### J-Link 烧录（推荐）

根目录执行 `.\flash-jlink.cmd`，等价于 `scripts/flash-jlink.ps1`。
`.bin` 按目标自动写偏移；`.elf` 由链接地址决定偏移。

#### 1. Standalone（快速调试）

```powershell
.\build.cmd
.\flash-jlink.cmd                          # 默认 Target=standalone → car-4wd.bin @ 0x0
.\flash-jlink.cmd -CarProject car-2wd      # 两轮
```

#### 2. 分区 — 产线首次（三镜像全烧）

```powershell
.\build.cmd -Target all -CarProject car-4wd

.\flash-jlink.cmd -Target bootloader       # bootloader.bin @ 0x00000000
.\flash-jlink.cmd -Target app              # app.bin        @ 0x00004000
.\flash-jlink.cmd -Target factory          # factory.bin    @ 0x00021000
```

顺序不限；**Boot 必须先存在于 0x0**，否则 APP_A / APP_B 无法被 Boot 拉起。

#### 3. 分区 — 日常迭代（只改量产固件）

Boot 已在片上时，**只烧 app** 即可：

```powershell
.\build.cmd -Target app
.\flash-jlink.cmd -Target app
```

厂测区 `factory.bin` 不变；NVS 参数不会被擦除。

#### 4. 仅更新 Boot 或厂测

```powershell
.\build.cmd -Target bootloader    # 或 factory
.\flash-jlink.cmd -Target bootloader
# 或
.\flash-jlink.cmd -Target factory
```

#### 5. Boot 单独测试（清空其它固件）

先**全片擦除**，再**只烧 Boot**，用于确认 `0x0` 上跑的是 Bootloader、且 APP 区为空时行为正确：

```powershell
.\build.cmd -Target bootloader
.\flash-jlink.cmd -Target bootloader -EraseAll
```

串口（UART7 / PE1，115200）**在复位前打开**，按板子复位键后应看到：

```text
[boot] start
[boot] no app
```

说明 Boot 正常、APP_A / APP_B 已被擦成 `0xFF`（无有效向量表），Boot 停住不跳转。
确认无误后再烧 app / factory：

```powershell
.\flash-jlink.cmd -Target app
.\flash-jlink.cmd -Target factory
```

完整分区跑通后，复位应看到 `[boot] start` → `[boot] jump 0x00004000` → APP 日志。

若只想清 APP 区、保留已有 Boot（不擦 `0x0..0x3FFF`），用 `-EraseApps`：

```powershell
.\flash-jlink.cmd -Target app -EraseApps    # 擦 APP+NVS 后只写 app.bin
```

### 烧录命令速查

| 命令 | 镜像 | Flash 偏移 |
|------|------|------------|
| `.\flash-jlink.cmd` | `car-4wd.bin`（standalone） | `0x0` |
| `.\flash-jlink.cmd -Target bootloader` | `bootloader.bin` | `0x0` |
| `.\flash-jlink.cmd -Target app` | `app.bin` | `0x4000` |
| `.\flash-jlink.cmd -Target factory` | `factory.bin` | `0x21000` |
| `.\flash-jlink.cmd -Target bootloader -EraseAll` | 全片擦除 + 仅 Boot | Boot 单独测试 |
| `.\flash-jlink.cmd -Target app -EraseApps` | 擦 APP+NVS 后写 app | 保留 Boot |

可选：`-CarProject car-4wd`（默认）或 `car-2wd`。

### UniFlash（可选）

```powershell
.\flash.cmd -Target standalone
```

`flash.cmd` 走 TI UniFlash（需本机安装 UniFlash 9.x 与根目录 `TM4C123GH6PM.ccxml`）。
分区多镜像更建议用 **J-Link** 分段烧录；详见 [docs/build.md](docs/build.md)。

### 烧录后验证（分区模式）

1. **UART7**（PE1，115200）看 Boot 日志：`[boot] jump 0x00004000`（默认 slot=A）。
2. 串口命令 `slot` → 应返回 `0`（APP_A）。
3. 发 `ftmenter` 或 **OK 键长按 10 s** → 复位后厂测心跳 `factory:heartbeat`。
4. 发 `ftmexit` 或再次 **OK 长按** → 回量产固件。

---

## 工程结构

```
bsp_driver/         MCU 外设薄封装
cbb/                芯片驱动子模块
projects/car-4wd/   .syscfg/  board/（设备库）  main/（应用）  build/
projects/car-2wd/   同上
Common/             公共模块（log、start、device_profile、nvs、cmd…）
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
| **[docs/build.md](docs/build.md)** | 编译、构建、烧录 |
| [docs/bluetooth-protocol.md](docs/bluetooth-protocol.md) | UART0 蓝牙二进制协议与 PC 工具 |
| [docs/syscfg-io-allocation.md](docs/syscfg-io-allocation.md) | 四轮/两轮引脚 |
| [docs/resource-allocation.md](docs/resource-allocation.md) | 定时器/DMA/中断 |
| [docs/project-overview.md](docs/project-overview.md) | 项目快照 |
| [docs/car-chassis-reference.md](docs/car-chassis-reference.md) | TivaWare API 参考 |
| [docs/flash-partition.md](docs/flash-partition.md) | Flash 分区 |

