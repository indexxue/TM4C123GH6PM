# STM32G431 OTA 产线标准作业程序（SOP）

**适用**：量产 APP-A + Bootloader 配对版本  
**OTA 通道**：USART1（PB6 TX / PB7 RX），115200 8N1  
**协议**：[`STM32G431_ota_uart_protocol.md`](STM32G431_ota_uart_protocol.md)  
**计划与测试矩阵**：[`STM32G431_bootloader_ota_plan.md`](STM32G431_bootloader_ota_plan.md) §10  
**二期（回滚/恢复）**：[`STM32G431_ota_phase2_plan.md`](STM32G431_ota_phase2_plan.md)、[`STM32G431_ota_release_notes.md`](STM32G431_ota_release_notes.md)  
**三期（外扩固件库）**：[`STM32G431_ota_phase3_plan.md`](STM32G431_ota_phase3_plan.md)

---

## 1. 硬件连接

| 项 | 要求 |
|----|------|
| 治具串口 | 接 **USART1** PB6/PB7，**不要**用 USB CDC 做 OTA |
| USB | 仅日志 / `serial_cmd` 产测（可选） |
| 供电 | 升级过程中保持供电稳定 |

---

## 2. 首次整板烧录（无 OTA）

1. `.\scripts\build.cmd -Target all`  
2. `.\scripts\flash.cmd -Image all`（推荐：Boot+App 配对，并清残留 meta）  
3. 确认 USB 心跳 / `app:ready`  

**禁止**：`.\scripts\flash.cmd -Image app-b`（片内 APP-B 已取消，脚本会拒绝）。  
**注意**：仅烧 App **不够**（二期 Boot 负责 backup/回滚/写后 CRC）；首次上二期或改 Boot 后必须 `-Image all`。

---

## 3. 产线 OTA 升级步骤

### 3.1 版本要求

- 设备内 Boot 须含 `boot_ota.c`（外扩 → 片内 APP-A）。  
- 主机 HEX 为 **APP-A** 链接（`0x08004000` … `0x0801BFFF`），≤ 96KB。  
- **Boot 与 App 版本表**由发布流程维护，治具脚本记录 `version` 字符串。

### 3.2 执行命令

```powershell
cd <repo>
.\scripts\build.cmd -Target app
python tools\ota_uart\ota_uart_host.py COMx MDK-ARM\STM32G431CBT6\STM32G431CBT6.hex 1.2.3
```

将 `COMx` 换为治具口，`1.2.3` 为产线版本号（写入 `ota_meta.version`）。

### 3.3 期望现象

| 阶段 | 期望 | 超时 |
|------|------|------|
| START / DATA | 主机打印 ACK，无 NACK | 单帧 15s |
| END | `OTA ACK received` | 30s |
| 复位后 | USB 恢复心跳（若接 USB） | — |
| Boot 烧片内 | 无用户操作，自动完成 | **30–90 s** |
| 新固件 | 版本 / 功能符合发布说明 | — |

### 3.4 失败处理

| 现象 | 处理 |
|------|------|
| NACK `status=4` CRC | 检查 HEX 与 CRC 计算；重传 |
| NACK `status=2` PARAM | 检查包序、offset、CHUNK≤508 |
| END 无 ACK | 查栈/外扩；重编译烧录 APP |
| ACK 后长时间无启动 | 确认已烧新版 Boot；ST-Link 救砖 boot+app |
| 死机无 USB | 可能旧固件 APP 内烧片内 → 救砖 |

救砖（ST-Link）：

```powershell
.\scripts\flash.cmd -Image all
```

无 ST-Link、片内已损坏且无 backup：**LED 慢闪** → 用 USART1 对 Boot 恢复模式重新整包 OTA（与 App 相同 `ota_uart_host.py`），或返厂 ST-Link。

---

## 4. 二期：回滚与验证（建议每版 Boot+App 发布前）

### 4.1 配对发布

- Boot Code ≤ 16 KB；与 App **同批发布**（见 [`STM32G431_ota_release_notes.md`](STM32G431_ota_release_notes.md)）。  
- 产线治具记录 `ota_meta.version`；回滚后版本应回到 **`confirmed_version`**。

### 4.2 回滚抽测（T10，研发）

1. 完成一次成功 OTA，确认 `backup_valid=0x4241434B`（`ota_meta_dump.py`）。  
2. `.\scripts\prepare_t10_test.cmd` → 仅编/烧 App。  
3. `python tools\ota_uart\ota_regression.py COMx --t10`（或手动 OTA `reg-t10-b` + 复位 4 次）。  
4. `ota_meta_dump.py`：`state=CONFIRMED`，`version` 为旧版。  
5. `.\scripts\restore_t10_test.cmd` → `build + flash -Image all`。

**产线禁止** 带 `OTA_TEST_SKIP_CONFIRM=1` 的 App 出货。

---

## 5. 回归抽测（建议每批 / 每版固件）

### 5.1 自动（USART1）

固件须含 **30s 无 DATA 超时**（`COMMON_OTA_DATA_IDLE_TIMEOUT_MS`）。

```powershell
python tools\ota_uart\ota_regression.py COMx --all
python tools\ota_uart\ota_regression.py --phase3 --offline
python tools\ota_uart\ota_regression.py --phase3 --usb-port COMy
python tools\ota_uart\ota_meta_dump.py
```

覆盖：T4 / ABORT / 30s 超时、T7、T11 UART（`--all`）；T10/T12 见二期计划。

### 5.2 手动

| 用例 | 步骤 | 期望 |
|------|------|------|
| **T3** 整包成功 | `ota_uart_host.py` 全量升级 | 新版本运行 |
| **T3b** 通道隔离 | OTA 中 USB 发 `sn` | 有应答，OTA 不中断 |
| **T5** 遗留 APP_B 标志 | ST-Link 写 `0x0801FFFC` = `0xAAAAAAAA` 复位 | 仍进 APP-A |
| **T6** NVS | OTA 前后 `sn` | SN 不变 |

---

## 6. 三期：外扩固件库灌槽与厂测切换

**通道**：USB CDC（`serial_cmd`）；**不用** USART1。  
**脚本**：`tools/fw_slot/fw_slot_program.py`（禁止裸写 bin 到槽区）。

### 6.1 推荐产线顺序

1. `.\scripts\flash.cmd -Image all` — Boot + 量产 APP-A（**不写外扩槽**）。  
2. USB 灌槽（需 `ftmenter` / `fw apply` 时 **必做**）：

```powershell
python tools\fw_slot\fw_slot_program.py --slot prod --hex MDK-ARM\STM32G431CBT6\STM32G431CBT6.hex --version 1.2.3 --port COMx
python tools\fw_slot\fw_slot_program.py --slot factory --hex Factory\MDK-ARM\Factory\Factory.hex --version ft-1.0.0 --port COMx
```

3. 首件 USB 验证：

```
fw list
ftmenter
fw apply prod
```

### 6.2 期望回复

| 命令 | 成功 | 失败（不复位） |
|------|------|----------------|
| `fw list` | `fw:list prod,1,... factory,1,...` | `factory,0,,0` → 未灌槽 |
| `ftmenter` | `ftmenter:reboot` | `no_image` / `busy,<state>` |
| `fw apply prod` | `fw apply:prod,reboot` | 同上 |

### 6.3 注意

- 片内须为 **含 `fw_slot` 的三期固件**；槽版本与片内版本可不同，但 **门禁逻辑以片内为准**。  
- `flash -Image all` **不擦**外扩槽；升级片内后槽一般 **无需**重灌。  
- 厂测双路径：外扩槽 `ftmenter`（推荐）或 ST-Link 烧 `Factory.hex`（救砖）。

**板测（2026-05-29）**：T21 灌槽、T22 `fw apply prod`、T23 `ftmenter`、T24 往返 — **PASS**。

---

## 7. 修订记录

| 日期 | 说明 |
|------|------|
| 2026-05-28 | 初版：产线 OTA 步骤、救砖、回归引用 |
| 2026-05-28 | 二期：`flash -Image all`、回滚抽测 T10、Boot recovery 救砖 |
| 2026-05-29 | 三期：外扩灌槽、`ftmenter` / `fw apply` 厂测切换 |
