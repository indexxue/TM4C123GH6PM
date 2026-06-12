# TM4C123GH6PM Flash 分区表

> **芯片**：256 KB 片上 Flash · 32 KB SRAM  
> **方案**：Bootloader + **APP_A（唯一运行槽）** + **APP_B（厂测 + OTA 存储）** + NVS（**纯片内**）  
> **详细设计**：[docs/flash-partition.md](docs/flash-partition.md) · **开发计划**：[docs/ab-ota-dev-plan.md](docs/ab-ota-dev-plan.md)

---

## 内存地图

```
地址          大小      分区          说明
───────────  ────────  ────────────  ──────────────────────────
0x00000000   16 KB     bootloader    复位入口；校验 APP_A；OTA 时 B→A 拷贝
0x00004000  116 KB     app_a         唯一应用运行槽（主业务固件）
0x00021000  116 KB     app_b         非运行区：OTA 暂存 + 厂测镜像存储
0x0003E000    8 KB     nvs           参数 + ota_meta 状态（不参与链接）
0x00040000   (末尾)
```

**与旧「片上 A/B 乒乓」差异**：Boot **始终跳转 APP_A**；APP_B **永不作为运行槽**，仅存放待激活镜像。

---

## APP_B 逻辑子区（116 KB @ `0x00021000`）

| 子区 | 偏移（相对 APP_B） | 大小 | 用途 |
|------|-------------------|------|------|
| `staging` | `+0` | 116 KB | OTA 下载缓冲；镜像从 `+512` 起（可选 512 B 槽头） |
| `factory` | 与 staging **互斥** | 同区复用 | 厂测固件预烧或 OTA 写入；**不同时**存放 OTA 包与厂测包 |

---

## 分区常量

```c
/* include/flash_layout.h（规划） */

#define FLASH_BL_BASE           0x00000000U
#define FLASH_BL_SIZE           0x00004000U   /* 16 KB */

#define FLASH_APP_A_BASE        0x00004000U
#define FLASH_APP_A_SIZE        0x0001D000U   /* 116 KB — 唯一运行槽 */
#define FLASH_APP_A_END         0x00020FFFU

#define FLASH_APP_B_BASE        0x00021000U
#define FLASH_APP_B_SIZE        0x0001D000U   /* 116 KB — 存储区，非运行 */
#define FLASH_APP_B_END         0x0003DFFFU

#define FLASH_STAGING_BASE      FLASH_APP_B_BASE
#define FLASH_STAGING_SIZE      FLASH_APP_B_SIZE
#define FLASH_OTA_IMAGE_MAX     FLASH_APP_A_SIZE   /* 单镜像 ≤ 116 KB */

#define FLASH_NVS_BASE          0x0003E000U
#define FLASH_NVS_SIZE          0x00002000U   /* 8 KB */

/* 运行槽恒为 A；APP_B 仅 staging / factory 语义 */
#define FLASH_RUN_SLOT          FLASH_APP_A_BASE
```

---

## 构建产物与烧录偏移

| 产物 | 链接脚本 | 烧录偏移 | 说明 |
|------|----------|----------|------|
| `build/bootloader.bin` | `ld/bootloader.ld` | `0x00000000` | Boot |
| `build/app.bin` | `ld/app.ld` | `0x00004000` | **主应用**（链 APP_A） |
| `build/factory.bin` | `ld/factory.ld` | `0x00021000` | 厂测镜像（存 APP_B，不直接运行） |
| NVS | — | 运行时初始化 | `ota_meta` 首次写入 |

```powershell
# 构建
.\scripts\build.ps1                  # standalone
.\scripts\build.ps1 -Target all      # bootloader + app + factory

# 烧录（自动匹配偏移）
.\flash.cmd                          # standalone @ 0x0
.\flash.cmd -Target prod             # Boot + APP_A
.\flash.cmd -Target app              # 仅 APP_A（须已有 Boot）
.\flash.cmd -Target all              # Boot + APP_A + 厂测
```

---

## 约束摘要

| 项 | 值 |
|----|-----|
| Flash 擦除块 | 1 KB |
| Flash 写入对齐 | 4 B（字） |
| 分区对齐 | 4 KB |
| 主应用上限 | **116 KB**（`.text` + `.rodata` + `.data` 加载区） |
| 运行槽 | **恒为 APP_A**；`VTOR = 0x00004000` |
| OTA 下载目标 | **APP_B（staging）**；运行中 APP **禁止**擦写 APP_A |
| 片内激活 | **仅 Bootloader**：staging（APP_B）→ 擦写 APP_A |
| 厂测切换 | `ftmenter`：校验 APP_B 厂测镜像 → `OTA_READY` → Boot 搬运 B→A |

---

## 修订

| 日期 | 说明 |
|------|------|
| 2026-06-12 | 片上 A/B 乒乓定稿 |
| 2026-06-12 | **改为 APP_A 单槽运行 + APP_B 厂测/OTA 存储** |
| 2026-06-12 | 移除外扩 Flash 方案，**纯片内**四段布局 |
