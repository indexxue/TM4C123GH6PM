# 厂测分区切换开发计划

> car-4wd · Boot + APP_A（量产）+ APP_B（厂测）+ NVS slot 直跳  
> 分区见 [PARTITION.md](../PARTITION.md)

---

## 状态

| 项 | 状态 |
|----|------|
| 分区 / 构建 | ✅ |
| Boot slot 跳转 + fallback | ✅ |
| NVS `nvs_boot_slot_*` | ✅ |
| `ftmenter` / `ftmexit` / `slot` 命令 | ✅ |
| OK 键长按切换 | ✅ |
| 文档 | ✅ |

---

## 验收

1. 烧录 Boot + app + factory
2. 默认进量产；`ftmenter` 或 OK 长按 → 厂测心跳
3. `ftmexit` 或 OK 长按 → 回量产
4. `cfg show` / SN 切换前后不变
5. 擦除 factory 区后 `ftmenter` 失败；Boot fallback 至 APP_A

---

## 目录

```
include/boot_slot.h
include/boot_image.h
bootloader/bootloader.c      — 精简 Boot，8 MHz 时钟，slot 跳转
Common/src/nvs.c          — nvs_boot_slot_get/set
Common/src/cmd.c          — ftmenter/ftmexit、cmd_boot_slot_switch
factory/                  — 厂测 @ APP_B
```
