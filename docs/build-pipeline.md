# 编译流水线：脚本 → 产物（TM4C123）

本文描述本仓库从环境检测到生成可烧录镜像的路径。约定对齐 STM32G474 参考仓库体验，实现载体为 **TM4C123GH6PM + TivaWare + FreeRTOS**。

| 项 | 值 |
|----|-----|
| MCU | TM4C123GH6PM（Cortex-M4F） |
| 产品 | `car-4wd` / `car-2wd` |
| 推荐入口 | WSL2：`projects/build.sh` / `projects/flash.sh` |
| 备选入口 | Windows：`.\build.cmd` / `.\flash-jlink.cmd` |
| 编译器 | `arm-none-eabi-gcc`（Windows `tools/bin/`，由 build.ps1 安装） |
| 烧录 | J-Link（`flash.sh` → `flash-jlink.ps1`）/ UniFlash（`flash.cmd`） |

---

## 1. 总览

```text
[编辑源码并保存]
        │
        ▼
 projects/build.sh  或  .\build.cmd
        │  解析产品 / 版本 / LOG_ENABLE / IMAGE_TARGET
        ▼
 scripts/build.ps1 -CarProject=<p> -Target=<image> ...
        │  gen_config.py → gcc → link
        ▼
 projects/<product>/build/<name>.{elf,hex,bin,map}
        │
        └─ release（车型）：
             bootloader.{bin,hex}
             TM4C123GH6PM_<date>_<car>_<ver>_unsigned.{bin,hex}       APP_A
             TM4C123GH6PM_<date>_<car>_<ver>_unsigned_full.{bin,hex}  三合一
             TM4C123GH6PM_<date>_factory.{bin,hex}                    共享 APP_B（car-4wd/factory）
           release（factory）：TM4C123GH6PM_<date>_factory.{bin,hex}
```

`<name>`：`standalone` 时等于产品名（如 `car-4wd`）；否则为 `bootloader` / `app` / `factory`。

---

## 2. WSL 命令

```bash
cd projects
./build.sh detect
./build.sh factory
./build.sh car-4wd
./build.sh car-4wd rebuild
./build.sh car-4wd release 0.1.0
./build.sh all release 0.1.0
IMAGE_TARGET=app ./build.sh car-4wd
./flash.sh car-4wd
./flash.sh factory
```

| 动作 | 行为 | 产物 |
|------|------|------|
| `build`（默认） | `LOG_ENABLE=1`，增量（当前实现为全量重编） | `projects/<p>/build/*.{elf,hex,bin}` |
| `rebuild` | 先 `clean` 再 build | 同上 |
| `clean` | 删除 `projects/<p>/build/` | — |
| `release [ver]` | `LOG_ENABLE=0` + 打包 | `release/<ver>/...` |

说明：`build.sh` 通过 `powershell.exe` 调用 `build.ps1`，因此需要 **WSL2 跑在 Windows 主机**，且本机已具备（或允许自动安装）`tools/` 与 `sdk/`。

---

## 3. Windows 命令

```powershell
.\build.cmd
.\build.cmd -CarProject car-2wd -Target standalone
.\build.cmd -CarProject car-4wd -Action release -FwVersion 0.1.0
.\build.cmd -CarProject car-4wd -Action detect
.\build.cmd -CarProject car-4wd -Action clean
```

---

## 4. 宏与版本

| 宏 | 来源 |
|----|------|
| `FW_VERSION_STR` | git semver tag / `-FwVersion` / `FW_VERSION` / `0.0.0-dev` |
| `FW_PRODUCT_NAME` | 产品目录名 |
| `FW_BUILD_DATE` / `FW_BUILD_TIME` | 构建时刻 |
| `LOG_ENABLE` | debug=`1`；release=`0`（`log.h` 编译剔除 `LOG_*`） |
| `DEVICE_PRODUCT_ID` | 1=四轮，2=两轮 |

版本规则摘要：

- **debug**：HEAD 精确 tag → 最近祖先 tag → `0.0.0-dev`
- **release**：显式 `<ver>` 不得低于仓库最高 semver tag（否则抬高）；省略则用 git / `FW_VERSION`

---

## 5. 日常最短路径

```bash
cd projects && ./build.sh car-4wd && ./flash.sh car-4wd
# 串口：UART7 115200（日志/CMD）；UART0 蓝牙协议
```

发正式包：

```bash
./build.sh car-4wd release 0.1.0
# → bootloader.* + …_car-4wd_…_unsigned{,_full}.* + …_factory.*
```

推送到 GitHub Tag + Release：

```bash
./build.sh all release 0.1.0
./publish.sh 0.1.0
# → git tag v0.1.0 + git push + gh release create（上传 release/0.1.0/*）
```

Windows：`.\publish-release.cmd 0.1.0`（需先 `gh auth login`）。  
完整说明见 [build.md](build.md)「发布到 GitHub Tag / Release」。

完整环境与分区说明见 [build.md](build.md)。
