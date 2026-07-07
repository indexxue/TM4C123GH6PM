# SDK（本地依赖，不提交 Git）

首次运行 `.\build.cmd` 时，构建脚本会自动将 **TivaWare C Series 2.2.0.295** 安装到此目录：

```
sdk/TivaWare_C_Series-2.2.0.295/
```

安装顺序：

1. 若已存在则跳过
2. 从本机旧路径 `D:\Ti\TivaWare_C_Series-2.2.0.295` 复制（迁移）
3. 从 `downloads/SW-TM4C-2.2.0.295.exe` 解压（需事先从 [TI 官网](https://www.ti.com/tool/SW-TM4C) 下载）

手动安装：

```powershell
.\scripts\install-tivaware.ps1 -InstallerPath D:\path\to\SW-TM4C-2.2.0.295.exe
```

完整说明见 [docs/build.md](../docs/build.md)。
