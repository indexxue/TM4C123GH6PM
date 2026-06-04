<#
.SYNOPSIS
    安装 TivaWare C Series SDK (SW-TM4C)。
.DESCRIPTION
    TivaWare 需要从 TI 官网下载（需 TI 账号登录）。
    当前预期路径:
      D:\Ti\TivaWare_C_Series-2.2.0.295\docs\sw\sysconfig\products.json

    下载地址:
      https://www.ti.com/tool/SW-TM4C

    下载后运行此脚本完成部署：
      .\scripts\install-tivaware.ps1 -InstallerPath D:\Downloads\SW-TM4C-2.2.0.295.exe
#>
param(
    [string]$InstallerPath = "",
    [string]$TargetDir = "D:\Ti\TivaWare_C_Series-2.2.0.295"
)

$ErrorActionPreference = "Stop"

# 检查是否已安装
if (Test-Path (Join-Path $TargetDir "docs\sw\sysconfig\products.json")) {
    Write-Host "TivaWare already installed at $TargetDir" -ForegroundColor Green
    exit 0
}

if ([string]::IsNullOrEmpty($InstallerPath)) {
    Write-Host @"

TivaWare C Series 需要手动下载。

1. 打开: https://www.ti.com/tool/SW-TM4C
2. 登录 myTI 账号，下载 SW-TM4C-2.2.0.295.exe (~200 MB)
3. 运行此脚本:
   .\scripts\install-tivaware.ps1 -InstallerPath D:\path\to\SW-TM4C-2.2.0.295.exe

"@ -ForegroundColor Yellow
    exit 1
}

if (-not (Test-Path $InstallerPath)) {
    throw "Installer not found: $InstallerPath"
}

Write-Host "Installing TivaWare to $TargetDir ..." -ForegroundColor Cyan
$tempDir = Join-Path (Split-Path $TargetDir -Parent) "_tivaware_temp"
New-Item -ItemType Directory -Force -Path $tempDir | Out-Null

# SW-TM4C 是自解压 exe，可用 7z 或静默解压
Start-Process -FilePath $InstallerPath -ArgumentList "/S", "/D=$tempDir" -Wait -NoNewWindow

# 找到解压后的目录
$extracted = Get-ChildItem $tempDir -Filter "*TivaWare*" -Directory | Select-Object -First 1
if ($extracted) {
    Move-Item $extracted.FullName $TargetDir -Force
} else {
    # 尝试直接移动
    New-Item -ItemType Directory -Force -Path $TargetDir | Out-Null
    Get-ChildItem $tempDir | Move-Item -Destination $TargetDir -Force
}
Remove-Item $tempDir -Recurse -Force

Write-Host "TivaWare installed." -ForegroundColor Green
Write-Host "Product JSON: $(Join-Path $TargetDir 'docs\sw\sysconfig\products.json')"