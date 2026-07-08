# TM4C123GH6PM Flash 分区表

> **芯片**：256 KB 片上 Flash · 32 KB SRAM · **8 MHz 晶振 → 80 MHz**
> **方案**：Bootloader + **APP_A（量产）** + **APP_B（厂测）** + NVS（**纯片内**）  
> **详细设计**：[docs/flash-partition.md](docs/flash-partition.md)

---

## 内存地图

```
地址          大小      分区          说明
───────────  ────────  ────────────  ──────────────────────────
0x00000000   16 KB     bootloader    复位入口；读 NVS slot 选槽跳转
0x00004000  116 KB     app_a         量产固件（car-4wd app.bin）
0x00021000  116 KB     app_b         厂测固件（factory.bin）
0x0003E000    8 KB     nvs           参数 + Boot 保留区 slot
0x00040000   (末尾)
```

**运行模型**：Boot 读 NVS `slot`（0=A / 1=B），**直接跳转**对应分区；目标槽镜像无效时 **fallback** 至另一槽。

---

## 厂测切换

| 方向 | 触发 | 行为 |
|------|------|------|
| 量产 → 厂测 | `ftmenter` 或 **OK 键长按 10 s** | 校验 APP_B → `slot=1` → 复位 |
| 厂测 → 量产 | `ftmexit` 或 **OK 键长按 10 s** | 校验 APP_A → `slot=0` → 复位 |

产线预烧：`bootloader.bin` @ 0x0、`app.bin` @ 0x4000、`factory.bin` @ 0x21000。

OK 键为 PE2 ADC 分压按键（与 UP/DN 共用一路 ADC）。

---

## 分区常量

```c
/* include/flash_layout.h */

#define FLASH_BL_BASE           0x00000000U
#define FLASH_BL_SIZE           0x00004000U   /* 16 KB */

#define FLASH_APP_A_BASE        0x00004000U
#define FLASH_APP_A_SIZE        0x0001D000U   /* 116 KB */

#define FLASH_APP_B_BASE        0x00021000U
#define FLASH_APP_B_SIZE        0x0001D000U   /* 116 KB */

#define FLASH_NVS_BASE          0x0003E000U
#define FLASH_NVS_SIZE          0x00002000U   /* 8 KB */
```

运行槽由 `include/boot_slot.h` + NVS Boot 保留区（256 B）持久化。

---

## 构建产物与烧录偏移

| 产物 | 链接脚本 | 烧录偏移 | 说明 |
|------|----------|----------|------|
| `build/bootloader.bin` | `ld/bootloader.ld` | `0x00000000` | Boot |
| `build/app.bin` | `ld/app.ld` | `0x00004000` | 量产 APP_A |
| `build/factory.bin` | `ld/factory.ld` | `0x00021000` | 厂测 APP_B |
| NVS | — | 运行时初始化 | 参数区首次写入 |

```powershell
# 构建
.\scripts\build.ps1 -Target all      # bootloader + app + factory

# 烧录
.\flash-jlink.cmd -Target bootloader
.\flash-jlink.cmd -Target app         # 日常可只烧 app（须已有 Boot）
.\flash-jlink.cmd -Target factory      # 产线预置厂测
```

---

## 约束摘要

| 项 | 值 |
|----|-----|
| Flash 擦除块 | 1 KB |
| 主应用上限 | **116 KB** |
| 默认 slot | **0（APP_A）** |
| 无效槽 | fallback 至另一槽 |

---

## 修订

| 日期 | 说明 |
|------|------|
| 2026-06-12 | 片上四段布局定稿 |
| 2026-07-08 | 双槽直跳厂测切换；移除 OTA |
