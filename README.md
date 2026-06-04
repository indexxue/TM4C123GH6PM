# TM4C123GH6PM 裸机项目模板

基于 **TM4C123G LaunchPad** (Cortex-M4F) 的嵌入式 C 项目框架，
依赖同级 `tm4c123gh6pm-sdk/` 目录提供 ARM 交叉工具链和启动代码。

## 目录结构

```
tm4c123-project/
├── .vscode/                ← Cursor / VS Code 工作区配置
│   ├── tasks.json          ←   Ctrl+Shift+B 编译 / 清理 / 烧录
│   ├── launch.json         ←   GDB 调试 (OpenOCD / J-Link)
│   ├── c_cpp_properties.json ← IntelliSense
│   └── settings.json
├── src/
│   ├── main.c              ← 应用入口 (LED 闪烁 + 按键切换颜色)
│   ├── syscalls.c          ← newlib 裸机系统调用存根
│   └── systick.c           ← SysTick 延时驱动
├── include/
│   ├── gpio.h              ← GPIO 便捷宏 (SetPin / TogglePin / LED 快捷)
│   └── systick.h           ← SysTick API + 寄存器定义
├── scripts/
│   └── build.ps1           ← 纯 PowerShell 编译脚本 (无需 make)
├── Makefile                ← GNU Make 编译
└── .gitignore
```

## 编译

**方式 A (PowerShell，推荐)**

```powershell
cd D:\Ti\tm4c123-project
powershell -ExecutionPolicy Bypass -File .\scripts\build.ps1
```

**方式 B (Make)**

```powershell
cd D:\Ti\tm4c123-project
make
```

## 在 Cursor 中使用

1. 用 Cursor 打开 `D:\Ti\tm4c123-project` 目录
2. **Ctrl+Shift+B** → 选择 "Build"
3. C/C++ IntelliSense 已配置为 SDK 的
   `arm-none-eabi-gcc`，自动识别 Cortex-M4
4. 调试需先启动 GDB server (OpenOCD / J-Link)，
   然后按 F5 选择对应配置

## 烧录

系统已安装 UniFlash 9.2.0：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\flash-uniflash.ps1 -Image build\tm4c123-project.bin
```

首次烧录需在 UniFlash GUI 中创建 TM4C123GH6PM 目标配置 (.ccxml)。

## 硬件连接

| 引脚 | 功能 |
|------|------|
| PF1  | 红色 LED |
| PF2  | 蓝色 LED |
| PF3  | 绿色 LED |
| PF4  | SW1 (按键，按下=低) |
| PF0  | SW2 (按键，按下=低) |
