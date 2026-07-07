<#
.SYNOPSIS
    安装 TivaWare C Series SDK 到项目本地 sdk/ 目录。
.DESCRIPTION
    首次构建时由 build.ps1 自动调用。安装顺序：
      1. 已存在于 sdk/TivaWare_C_Series-2.2.0.295
      2. 从本机旧路径 D:\Ti\TivaWare_C_Series-2.2.0.295 复制（迁移）
      3. 从 downloads/SW-TM4C-2.2.0.295.exe 解压
      4. -InstallerPath 指定的安装包

    TI 安装包需 myTI 账号下载：
      https://www.ti.com/tool/SW-TM4C
#>
param(
    [string]$InstallerPath = ""
)

$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "sdk-path.ps1")

$Marker = $Script:TivaWareLib
$TargetDir = $Script:TivaWareRoot
$DownloadsDir = Join-Path $Script:ProjectRoot "downloads"
$DefaultInstaller = Join-Path $DownloadsDir "SW-TM4C-$($Script:TivaWareVersion).exe"

function Install-FromExe {
    param(
        [string]$ExePath,
        [string]$DestDir
    )

    if (-not (Test-Path $ExePath)) {
        throw "Installer not found: $ExePath"
    }

    Write-Host "Extracting TivaWare from $ExePath ..." -ForegroundColor Cyan
    $tempDir = Join-Path $Script:SdkDir "_tivaware_temp"
    if (Test-Path $tempDir) {
        Remove-Item $tempDir -Recurse -Force
    }
    New-Item -ItemType Directory -Force -Path $tempDir | Out-Null

    Start-Process -FilePath $ExePath -ArgumentList "/S", "/D=$tempDir" -Wait -NoNewWindow

    $extracted = Get-ChildItem $tempDir -Filter "*TivaWare*" -Directory | Select-Object -First 1
    if ($extracted) {
        if (Test-Path $DestDir) {
            Remove-Item $DestDir -Recurse -Force
        }
        Move-Item $extracted.FullName $DestDir -Force
    } else {
        New-Item -ItemType Directory -Force -Path $DestDir | Out-Null
        Get-ChildItem $tempDir | Move-Item -Destination $DestDir -Force
    }

    Remove-Item $tempDir -Recurse -Force
}

if (Test-TivaWareInstalled) {
    Write-Host "TivaWare already installed at $TargetDir" -ForegroundColor Green
    exit 0
}

New-Item -ItemType Directory -Force -Path $Script:SdkDir, $DownloadsDir | Out-Null

if (Test-TivaWareInstalled -Root $Script:LegacyTivaWareRoot) {
    Write-Host "Copying TivaWare from $($Script:LegacyTivaWareRoot) ..." -ForegroundColor Cyan
    Copy-Item $Script:LegacyTivaWareRoot $TargetDir -Recurse -Force
    Write-Host "TivaWare installed at $TargetDir" -ForegroundColor Green
    exit 0
}

if ([string]::IsNullOrEmpty($InstallerPath)) {
    $InstallerPath = $DefaultInstaller
}

if (Test-Path $InstallerPath) {
    Install-FromExe -ExePath $InstallerPath -DestDir $TargetDir
    if (Test-TivaWareInstalled) {
        Write-Host "TivaWare installed at $TargetDir" -ForegroundColor Green
        exit 0
    }
    throw "Installer finished but SDK marker not found at $Marker"
}

Write-Host @"

TivaWare C Series SDK 未找到，且无法自动下载（需 TI myTI 账号）。

请任选一种方式：

1. 将已下载的安装包放到：
     $DefaultInstaller
   然后重新运行 .\build.cmd

2. 手动指定安装包路径：
     .\scripts\install-tivaware.ps1 -InstallerPath D:\path\to\SW-TM4C-$($Script:TivaWareVersion).exe

3. 若本机已有全局安装，确保存在：
     $($Script:LegacyTivaWareRoot)

下载地址: https://www.ti.com/tool/SW-TM4C

"@ -ForegroundColor Yellow
exit 1
