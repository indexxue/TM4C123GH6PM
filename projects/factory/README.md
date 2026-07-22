# 厂测固件（Factory）

> 产品 ID **0**；链址 **APP_B `0x00021000`**，Boot 按 NVS slot 直接跳转运行。  
> 仅 UART7 **cmd** 外设调试 / NVS / 传感器探测；**无**蓝牙协议层。

## 角色

| 项 | 说明 |
|----|------|
| 产品目录 | `projects/factory/`（`DEVICE_PRODUCT_ID=0`） |
| 存储位置 | 片上 Flash APP_B（116 KB） |
| 链接脚本 | `ld/factory.ld` |
| 板级配置 | 本目录 `.syscfg/`（以 car-4wd 为模板） |
| 烧录偏移 | `0x00021000` |
| 进入厂测 | 量产侧 `ftmenter` 或 **OK 键长按 10 s** |
| 退出厂测 | `ftmexit` 或 **OK 键长按 10 s** → 回 APP_A |

## 构建与烧录

```bash
# WSL
cd projects
./build.sh factory
./flash.sh factory
```

```powershell
# 仓库根目录
.\build.cmd factory

# 或本目录
.\build.cmd
```

```powershell
.\flash-jlink.cmd -Target factory
```

产线全套需分别编译：

```powershell
.\build.cmd car-4wd -Target all    # bootloader + app
.\build.cmd factory                # 厂测 APP_B
```

## 相关文档

- [PARTITION.md](../../PARTITION.md)
- [docs/factory-partition-plan.md](../../docs/factory-partition-plan.md)
- [docs/flash-partition.md](../../docs/flash-partition.md)
- [docs/build.md](../../docs/build.md)
