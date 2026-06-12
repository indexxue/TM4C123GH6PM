# USB 默认串口 + 厂测 NVS 实施计划

**适用范围**：STM32G431CBT6 本仓库（APP-A `MDK-ARM/STM32G431CBT6.uvprojx`、厂测 `Factory/MDK-ARM/Factory.uvprojx`）。  
**关联文档**：[`STM32G431_development_standard.md`](STM32G431_development_standard.md)、[`Factory/README.md`](../Factory/README.md)。

---

## 一、目标

| 模块 | 当前 | 目标 |
|------|------|------|
| 日志 | `COMMON_LOG_UART` → USART1 | USB CDC VCP |
| 产测命令 | `COMMON_SERIAL_CMD_UART` → USART3 | 同一 USB VCP |
| 厂测 NVS | 已 `nvs_init()` | 种子数据（默认/随机）+ 启动打印 |
| USB 栈 | 仅 `MX_USB_PCD_Init()`，未链 Device 库 | CDC 枚举，可编译烧录 |

---

## 二、目标架构

```
PC 串口终端
    ↕ USB CDC
usbd_cdc_if (RX/TX)
    → serial_cmd_process_line / log 输出
    ← 统一 TX（互斥）
nvs_init / factory_nvs_seed
```

设计原则：

1. **单一默认控制台** = USB VCP；USART1/3 可保留 Cube 初始化，业务默认走 USB。
2. **RX 在 CDC 回调入队**，FreeRTOS 任务组行后解析命令。
3. **日志与 serial_cmd 共用 TX 互斥**，避免粘包。

---

## 三、分阶段任务

### 阶段 0：工程与启动（当前进行中）

| 步骤 | 内容 | 状态 |
|------|------|------|
| 0.1 | Keil 编入 `USB_DEVICE/`、`Middlewares/ST/STM32_USB_Device_Library/` | 已完成 |
| 0.2 | `main`：`MX_USB_Device_Init()` 替代单独 PCD 初始化 | 已完成 |
| 0.3 | `stm32g4xx_it.c`：`USB_LP_IRQHandler` → `HAL_PCD_IRQHandler` | 已完成 |
| 0.4 | 去除 `hpcd_USB_FS` / `HAL_PCD_MspInit` 重复（以 `usbd_conf.c` 为准） | 已完成 |
| 0.5 | APP-A、Factory 编译 0 Error | 已完成 |

### 阶段 1：`board_config.h` 开关

| 步骤 | 状态 |
|------|------|
| `COMMON_DEFAULT_CONSOLE_USB=1` 及衍生宏 | 已完成 |

### 阶段 2：`usb_console` 桥接层

| 步骤 | 状态 |
|------|------|
| `Common/Src/usb_console.c` TX 队列 + RX 组行 | 已完成 |
| `log.c` / `serial_cmd.c` 走 USB | 已完成 |
| `usbd_cdc_if.c` RX/TX 回调对接 | 已完成 |
| `app_init` / `factory_init` / 任务轮询 | 已完成 |
| APP-A / Factory 编译 0 Error | 已完成 |

### 阶段 3：FreeRTOS 任务

- **Factory** `factory_freertos.c`：USB 模式下 `usb_console_poll()` — **已在阶段 2 完成**。
- **APP-A** `app_freertos.c`：`defaultTask` 轮询 `usb_console_poll()` — **已在阶段 2 完成**。

### 阶段 4：厂测 NVS

- 新增 `Factory/Src/factory_nvs.c`：`factory_nvs_seed_test()`（RNG MAC/SN、region、device_type 等）。
- `factory_init()`：`nvs_init()` → seed → `nvs_print_boot_info()` / `factory_nvs_log_dump()`。
- 产测命令：`sn` / `mac` / `devtype` / `log` 经 USB 验证。

**种子策略（建议）**：仅当 key 不存在时写入；厂测强制覆盖可用 `BUILD_FACTORY` + `FACTORY_OVERWRITE_NVS` 宏控制。

### 阶段 5：文档与验收

更新 `STM32G431_development_standard.md`、`Factory/README.md`；验收见下文 checklist。

---

## 四、文件清单（全计划）

| 操作 | 路径 |
|------|------|
| 新建 | `Common/Inc/usb_console.h`, `Common/Src/usb_console.c` |
| 新建 | `Factory/Inc/factory_nvs.h`, `Factory/Src/factory_nvs.c` |
| 修改 | `board_config.h`, `log.c`, `serial_cmd.c`, `serial_cmd.h` |
| 修改 | `usbd_cdc_if.c`, `factory_init.c`, `factory_freertos.c`, `app_init.c` |
| 修改 | `main.c`（APP-A + Factory）, `stm32g4xx_it.c`, `usb.c`/`usb.h` |
| 修改 | `MDK-ARM/*.uvprojx`, `Factory/MDK-ARM/Factory.uvprojx` |

---

## 五、风险与对策

| 风险 | 对策 |
|------|------|
| `CDC_Transmit_FS` 返回 `USBD_BUSY` | TX 小队列 + `CDC_TransmitCplt_FS` 续发 |
| USB 未枚举即打日志 | 允许丢弃；`factory:ready` 放任务内延迟 |
| 双 `hpcd_USB_FS` | 仅 `usbd_conf.c` 定义 |
| 厂测随机 NVS 污染量产 | 仅 `BUILD_FACTORY` 写种子；APP-A 不写随机 MAC |
| Flash 分区变更 | `nvs_factory_reset()` |

---

## 六、验收 checklist

- [ ] APP-A、Factory 编译 0 Error
- [ ] PC 识别 USB CDC，无需 USART 线即可通信
- [ ] `LOG_INFO` 与 `serial_cmd` 同一 COM
- [ ] 厂测启动 NVS boot 日志 + 种子 `sn`/`mac`
- [ ] `sn` 写入、复位后保持
- [ ] `COMMON_DEFAULT_CONSOLE_USB=0` 可回退 UART

---

## 七、推荐实施顺序

1. 阶段 0（0.5～1 天）  
2. 阶段 1～2（1 天）  
3. 阶段 3（0.5 天）  
4. 阶段 4（0.5 天）  
5. 阶段 5 + 联调（0.5 天）

---

*文档版本：与阶段 0 代码变更同步；阶段完成后在表中勾选状态。*
