# TM4C123 片上 Flash 分区设计

> **选定方案**：**Bootloader + APP_A（量产）+ APP_B（厂测）+ NVS**，纯片内 Flash。  
> Boot 读 NVS `slot` 直接跳转 APP_A 或 APP_B；无效槽 fallback。  
> **速查**：[PARTITION.md](../PARTITION.md) · **开发计划**：[factory-partition-plan.md](factory-partition-plan.md)

---

## 1. 角色

| 分区 | 职责 |
|------|------|
| **Bootloader** | 读 NVS slot；校验目标槽向量表；跳转；无效则 fallback |
| **APP_A** | 量产业务固件（`app.ld` @ `0x00004000`） |
| **APP_B** | 厂测固件（`factory.ld` @ `0x00021000`） |
| **NVS** | 键值配置 + Boot 保留区 `boot_slot_cfg_t` |

---

## 2. 物理布局

| 名称 | 基址 | 大小 | 链接脚本 |
|------|------|------|----------|
| `bootloader` | `0x00000000` | 16 KB | `ld/bootloader.ld` |
| `app_a` | `0x00004000` | 116 KB | `ld/app.ld` |
| `app_b` | `0x00021000` | 116 KB | `ld/factory.ld` |
| `nvs` | `0x0003E000` | 8 KB | 不参与链接 |

主应用与厂测镜像均 ≤ **116 KB**（`.text` + `.rodata` + `.data` 加载区）。

---

## 3. Bootloader

**时钟**：`SYSCTL_XTAL_8MHZ` + `SYSDIV_2_5` → 80 MHz（与 APP / BSP 相同；Boot 代码尽量精简，不链 FreeRTOS / BSP）。

上电流程：

1. 时钟 + UART7 调试输出
2. `boot_slot_read()` → slot 0 或 1
3. 校验 `boot_slot_target_base(slot)` 向量表（`boot_image_is_valid`）
4. 无效 → 尝试另一槽（fallback）
5. 仍无效 → halt
6. 设 VTOR / MSP / PC 跳转

实现：`bootloader/bootloader.c`、`include/boot_slot.h`、`include/boot_image.h`。

---

## 4. 厂测切换

| 命令 / 操作 | 固件 | 行为 |
|-------------|------|------|
| `ftmenter` | 量产 @ APP_A | APP_B 有效 → `nvs_boot_slot_set(B)` → 停电机 → 复位 |
| `ftmexit` | 厂测 @ APP_B | APP_A 有效 → `nvs_boot_slot_set(A)` → 停电机 → 复位 |
| OK 长按 10 s | 两侧 | 同上（`BTN_PERMISSION_FTM`） |
| `slot` | 两侧 | 查询当前 NVS slot |

产线：三镜像预烧；NVS 中 SN / 校准在切换时 **不擦**。

---

## 5. NVS Boot 保留区

| 项 | 值 |
|----|-----|
| 偏移 | 页头后 256 B（`NVS_BOOT_RSVD_OFFSET`） |
| 结构 | `boot_slot_cfg_t`：`magic`（`SLOT`）+ `slot`（0/1） |
| APP API | `nvs_boot_slot_get()` / `nvs_boot_slot_set()` |
| 页轮换 | KV 提交时拷贝保留区；slot 写入走独立 commit |

---

## 6. 构建与日常开发

- 日常：`standalone` 或 `-Target app` 只烧 APP_A（需已有 Boot）
- 产线：`build.ps1 -Target all` + 分段烧录三 bin
- 编译宏：`FLASH_APP_A_SLOT`（量产）、`FLASH_FACTORY_SLOT`（厂测）

---

## 7. 修订

| 日期 | 说明 |
|------|------|
| 2026-06-12 | 四段布局 |
| 2026-07-08 | 双槽直跳厂测切换；移除 OTA |
