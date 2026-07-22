# 厂测固件（Factory / APP_B）

> 链址 **APP_B `0x00021000`**。应用源码共享；**板级跟车型 `.syscfg`**，不同项目不同 init。  
> 仅 UART7 **cmd / serial_cmd**；**无**蓝牙协议层。

## 模型

| 层 | 来源 |
|----|------|
| 应用 | `projects/factory/main/`（`app.c` + `serial_cmd.c`） |
| 板级 | 构建时选定的车型 `projects/<car>/.syscfg/` → `board/` |
| 档案 | `DEVICE_PRODUCT_ID` = 车型 id；`-DFLASH_FACTORY_SLOT` 叠加 CMD |

```
.\build.cmd car-4wd -Target factory   # 4 电机板，PRODUCT_ID=1
.\build.cmd car-2wd -Target factory   # 2 电机板，PRODUCT_ID=2
.\build.cmd factory                   # 兼容：factory/.syscfg（4wd 模板），id=0
```

产物：`projects/<car>/build/factory.bin`（`build factory` → `projects/factory/build/`）。

## 初始化门控

`Start_Init` / `App_Start` / `serial_cmd_register_defaults` 均读 `device_profile`：

| mask | 作用 |
|------|------|
| `BOARD_MOTOR` / `ENCODER` / `LINE` / `PERIPH` | `Start_Init` 拉电机/编码器/循迹/外设 |
| `BOARD_IMU` / `MAG` / `LED` / `BUZZER` / `ULTRA` / `BATTERY` | 厂测传感器与指示 |
| `PLATFORM_LOG` / `BUTTON` / `CMD` | 日志、按键、UART7 命令（厂测槽自动叠加 CMD） |

板级数量由 codegen：`BOARD_MOTOR_COUNT`、`BOARD_ENCODER_COUNT`、`LINE_SENSOR_COUNT`。

## 构建与烧录

```powershell
.\build.cmd car-4wd -Target factory
.\flash-jlink.cmd -Target factory -CarProject car-4wd

.\build.cmd car-2wd -Target factory
.\flash-jlink.cmd -Target factory -CarProject car-2wd
```

```bash
# WSL
cd projects
IMAGE_TARGET=factory ./build.sh car-4wd
IMAGE_TARGET=factory ./build.sh car-2wd
```

产线全套：

```powershell
.\build.cmd car-4wd -Target all
.\build.cmd car-4wd -Target factory
```

## UART7 命令（115200）

公共：`help` `reboot` `i2c` `motor` `adc` `ftmexit` …  
厂测扩展（按档案注册）：`sn` `devtype` `led` `buzzer` `bat` `enc` `imu` `mag` `ultra` `att` `ftm`

`devtype` 会打印产品名与 `BOARD_MOTOR_COUNT`。

## 相关文档

- [PARTITION.md](../../PARTITION.md)
- [docs/flash-partition.md](../../docs/flash-partition.md)
- [docs/build.md](../../docs/build.md)
