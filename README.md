# TM4C123GH6PM 裸机项目模板

基于 **TM4C123G LaunchPad** (Cortex-M4F) 的嵌入式 C 项目。
**git clone 后一条命令即可编译**，无需手动安装任何东西。

## 快速开始

```powershell
git clone <repo-url> tm4c-project
cd tm4c-project
.\scripts\build.ps1
```

**编译流程（自动）**：
1. 检测本地是否存在 `arm-none-eabi-gcc`
2. 如不存在，自动从 ARM 官方下载 GNU Toolchain 到 `tools/`
3. 编译项目源文件，输出到 `build/`

## 硬件

| 引脚 | 功能 |
|------|------|
| PF1  | 红色 LED |
| PF2  | 蓝色 LED |
| PF3  | 绿色 LED |
| PF4  | SW1 (按键，按下=低) |
| PF0  | SW2 (按键，按下=低) |

## 在 Cursor / VS Code 中使用

1. 用 Cursor 打开 `tm4c-project` 目录
2. **Ctrl+Shift+B** → 选择 "Build" 编译
3. IntelliSense 已自动配置为 Cortex-M4F (arm-none-eabi-gcc)

## 编译方式

**方式 A（推荐，无需 make）**
```powershell
.\scripts\build.ps1
```

**方式 B（需系统已安装 make）**
```powershell
.\scripts\install-toolchain.ps1    # 仅首次
make
```

## 烧录

需先通过 UniFlash GUI 创建 TM4C123GH6PM 目标配置 (`.ccxml`)，然后：
```powershell
.\scripts\flash-uniflash.ps1 -Image build\tm4c123-project.bin
```

## 调试

在 Cursor 中按 F5，选择对应 GDB Server：
- **OpenOCD**：监听 localhost:3333
- **J-Link**：监听 localhost:2331

## 项目结构

```
tm4c-project/
├── .vscode/                 ← Cursor 配置 (tasks/launch/IntelliSense)
├── src/
│   ├── main.c               ← 应用入口 (LED 闪烁 + 按键切换颜色)
│   ├── startup_tm4c123gh6pm.c ← 启动代码 + 中断向量表
│   ├── syscalls.c           ← newlib 裸机系统调用存根
│   └── systick.c            ← SysTick 延时驱动 (1ms 中断)
├── include/
│   ├── tm4c123gh6pm.h       ← TM4C123GH6PM 寄存器定义
│   ├── gpio.h               ← GPIO 便捷宏
│   └── systick.h            ← SysTick API
├── ld/
│   └── tm4c123gh6pm.ld      ← 链接脚本 (256K Flash / 32K SRAM)
├── scripts/
│   ├── build.ps1            ← 一键编译 (自动安装工具链)
│   └── install-toolchain.ps1 ← ARM GCC 下载/缓存/安装
├── tools/                   ← 自动下载的工具链 (gitignore)
├── download/                ← 下载缓存 (gitignore)
├── build/                   ← 编译输出 (gitignore)
└── Makefile
```

## 技术细节

- **MCU**：TM4C123GH6PM (Cortex-M4F @ 80 MHz, 256 KB Flash, 32 KB SRAM)
- **工具链**：ARM GNU Toolchain 14.3.Rel1 (arm-none-eabi-gcc 14.3.1)
- **架构**：ARMv7E-M + 硬件单精度浮点 (软浮点 ABI)
- **标准**：C11 + GNU 裸机扩展
