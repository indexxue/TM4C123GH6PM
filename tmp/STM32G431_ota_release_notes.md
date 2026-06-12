# STM32G431 OTA 发布说明（配对版本）

**日期**：2026-05-29  
**芯片**：STM32G431CBT6 + ZB25VQ16  

---

## 当前验证配对（三期 P0）

| 组件 | 工程 | 链接/大小 | Code 约（构建日志） |
|------|------|-----------|---------------------|
| **Bootloader** | `Bootloader/MDK-ARM/Bootloader.uvprojx` | `0x08000000`，16 KB | **~12.8 KB**（三期 **无代码变更**） |
| **APP-A** | `MDK-ARM/STM32G431CBT6.uvprojx` | `0x08004000`，96 KB | **~67 KB**（含 `fw_slot`） |
| **Factory** | `Factory/MDK-ARM/Factory.uvprojx` | `0x08004000`，96 KB | **~55 KB**（含 `fw_slot`，无 OAD） |

**烧录**：首次上二期/三期、或修改 Boot 后，必须 `.\scripts\flash.cmd -Image all`。  
**外扩灌槽**：`flash -Image all` **不写**固件库槽；须单独运行 `tools/fw_slot/fw_slot_program.py`（见 SOP §6）。

---

## 三期能力摘要（2026-05-29）

| 能力 | 说明 |
|------|------|
| 外扩固件库 | PROD + FACTORY 槽 @ `0x00140000` / `0x00160000` |
| USB 命令 | `fw list` / `fw info` / `fw apply prod\|factory` |
| `ftmenter` | **行为变更**：= `fw apply factory`（外扩槽）；**不再**写 `BOOT_SLOT_FLAG_FACTORY` |
| 产线灌槽 | `fw_slot_program.py` + USB `fwslot` 协议 |
| 激活链 | slot → staging → `OTA_READY` → **现有 Boot**（D2） |

---

## 二期能力摘要

| 能力 | 说明 |
|------|------|
| 外扩 backup | `0x00120000`，CONFIRMED 后或激活前快照 |
| 写后 CRC | Boot 编程后 `image_validate` 带 CRC |
| `boot_attempts` 回滚 | `PENDING_VERIFY` 超 3 次从 backup 恢复 |
| Boot 恢复模式 | 片内无效且无 backup：LED 慢闪 + USART1 写 staging |
| meta v2 | `struct_version=2`，`backup_valid=0x4241434B` |

---

## 板测状态

| 项 | 状态 |
|----|------|
| T11 首次 OTA + backup | PASS |
| V4 一期回归 `--all` | PASS |
| **T21–T24 三期**（灌槽、apply、ftmenter、往返） | **PASS 2026-05-29** |
| **T20 / T28 / T29 三期**（`--phase3 --usb-port`） | **PASS 2026-05-29** |
| T32 拷贝掉电 | ⬜ 手动 |
| T33 meta READY 门禁 | ⏭️ 跳过待补（`--skip-t33`） |
| T10 回滚 | 待跑（`--t10`） |
| T12 恢复模式 | 待跑（`--t12`） |
| `ota_regression.py --phase3` | 离线 + USB PASS（T33 加 `--skip-t33`） |

---

## 回归命令

```powershell
python tools/ota_uart\ota_regression.py COM3 --all
python tools/ota_uart\ota_regression.py --phase3 --offline
python tools/ota_uart\ota_regression.py --phase3 --usb-port COM4
python tools/ota_uart\ota_regression.py --phase3 --usb-port COM4 --skip-t33
python tools\ota_uart\ota_meta_dump.py
```

**P1 USART1 灌槽**（须重编 App 后）：

```powershell
python tools\ota_uart\ota_uart_host.py COM3 MDK-ARM\...\STM32G431CBT6.hex 1.2.3 --target-slot prod
```

---

## 修订

| 日期 | 说明 |
|------|------|
| 2026-05-28 | 初版：Boot/App 配对尺寸、二期能力、板测状态 |
| 2026-05-29 | 三期：`--phase3` USB T20/T28/T29 PASS；T33 `--skip-t33`；release note 更新 |
