# TM4C123GH6PM

## 项目介绍

基于 **TM4C123GH6PM**（Cortex-M4F）的裸机 C 工程，目标板为 **EK-TM4C123GXL LaunchPad**。

在 Cursor / VS Code 中开发，用 PowerShell 脚本构建，集成 **SysConfig** 板级配置与 **TivaWare DriverLib**。工具链首次编译时自动下载，源码纳入 Git，编译产物与生成文件不提交。

仓库：<https://github.com/indexxue/TM4C123GH6PM>

---

## 如何编译

**前提**：本地已安装 [TivaWare C Series 2.2.0.295](https://www.ti.com/tool/SW-TM4C)（默认路径 `D:\Ti\TivaWare_C_Series-2.2.0.295`）。

```powershell
# 克隆
git clone git@github.com:indexxue/TM4C123GH6PM.git
cd TM4C123GH6PM

# 编译
.\scripts\build.ps1
# 或（不受 PowerShell 执行策略限制）
.\build.cmd
```

输出位于 `build/`：

- `tm4c123-project.elf` — 调试
- `tm4c123-project.bin` — 烧录

烧录（需 [UniFlash](https://www.ti.com/tool/UNIFLASH)）：

```powershell
.\scripts\flash-uniflash.ps1 -Image build\tm4c123-project.bin
```

IDE 中按 **Ctrl+Shift+B** 亦可触发编译。

---

## 如何推送

```powershell
git add .
git commit -m "描述本次改动"
git push origin main
```

远端已配置为 `git@github.com:indexxue/TM4C123GH6PM.git`，当前分支为 `main`。

首次在本机 clone 若遇 `dubious ownership` 报错：

```powershell
git config --global --add safe.directory D:/Ti/tm4c123-project
```

---

## 开发规范

**目录职责**

| 路径 | 说明 |
|------|------|
| `src/` | 应用与运行时源码（`main.c`、启动代码等） |
| `include/` | 公共头文件 |
| `.syscfg/` | 板级配置（SysConfig / JSON），修改后需重新编译 |
| `scripts/` | 构建、烧录、工具安装脚本 |
| `src/generated/` | 构建生成，**不要手动编辑、不要提交** |

**不要提交**（已在 `.gitignore`）：`build/`、`tools/`、`downloads/`、`src/generated/`。

**代码**

- C11，遵循现有风格：4 空格缩进，DriverLib 用于外设，LaunchPad 快捷宏见 `include/gpio.h`
- 改引脚配置：优先改 `.syscfg/tm4c123gh6pm.syscfg` 或 `.syscfg/project.json`，不在 `main.c` 硬编码引脚
- 提交前本地编译通过：`.\scripts\build.ps1`

**提交信息**

- 使用简短中文或英文，说明「做了什么 / 为什么」
- 示例：`feat: 添加 UART 日志`、`fix: 修正 SW2 GPIO 解锁`
