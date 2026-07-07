# TM4C123 小车 — 项目概览

> 精简快照；编译细节见 [build.md](build.md)，引脚见 [syscfg-io-allocation.md](syscfg-io-allocation.md)。

---

## 定位

| 项 | 说明 |
|----|------|
| 芯片 | TM4C123GH6PM，Cortex-M4F @ 80 MHz |
| RTOS | FreeRTOS v8.2.3（TivaWare GCC ARM_CM4F） |
| 形态 | 四轮 / 两轮 MG513 电机智能小车（两个独立工程） |
| 仓库 | https://github.com/indexxue/TM4C123GH6PM |

---

## 目录结构

```
projects/
  car-4wd/     .syscfg/  board/（设备库）  main/（应用）  build/
  car-2wd/     同上
Common/        跨工程公共模块（log、nvs、ota、start、app…）
include/       芯片级头文件（FreeRTOSConfig、flash_layout、tm4c123gh6pm）
scripts/       build.ps1、gen_config.py、flash-*.ps1
ld/            链接脚本
bootloader/    Bootloader
factory/       厂测固件
docs/          设计文档
```

---

## 构建

```powershell
.\build.cmd                         # 默认 car-4wd
.\build.cmd -CarProject car-2wd
.\projects\car-4wd\build.cmd        # 在工程目录内编译
```

详见 [build.md](build.md)。

---

## 初始化顺序

```c
Start_Init();   /* Common/src/start.c：clock → bsp → board → log */
App_Start();    /* projects/<car>/main/app.c：业务任务 */
```

`Start_Init()` 内部按 `device_profile` 的 `board_mask` 调用 `Motor_Init()`、`Encoder_Init()`、`Line_Init()`、`Board_Periph_Init()`。

---

## 文档索引

| 文档 | 用途 |
|------|------|
| [build.md](build.md) | 编译、烧录、代码生成 |
| [syscfg-io-allocation.md](syscfg-io-allocation.md) | 四轮/两轮引脚 |
| [resource-allocation.md](resource-allocation.md) | 定时器/DMA/中断 |
| [car-chassis-reference.md](car-chassis-reference.md) | TivaWare API 参考 |
| [peripheral-selection.md](peripheral-selection.md) | 外设模块选型 |
| [flash-partition.md](flash-partition.md) | Flash 分区 |
| [ab-ota-dev-plan.md](ab-ota-dev-plan.md) | OTA 计划 |

---

## 当前进度

- [x] 双工程脚手架、SysConfig 代码生成、板级 Init 框架
- [x] Bootloader + NVS / ota_meta（M0~M2）
- [ ] 蓝牙遥控、IMU、编码器测速、PID、循迹闭环
- [ ] OTA 激活与协议（M3~M5）
