<#
.SYNOPSIS
    安装 ARM GNU Toolchain (arm-none-eabi-gcc)。
.DESCRIPTION
    1. 优先检查同级目录是否有已安装的工具链 (如 tm4c123gh6pm-sdk/tools)
    2. 其次检查本地下载缓存
    3. 最后从 ARM 官方 CDN 下载
#>
$ErrorActionPreference = "Stop"

$ProjectRoot = Split-Path -Parent $PSScriptRoot
$ToolsDir    = Join-Path $ProjectRoot "tools"
$DownloadsDir = Join-Path $ProjectRoot "downloads"

# ---- 已安装则直接返回 ----
if (Test-Path (Join-Path $ToolsDir "bin\arm-none-eabi-gcc.exe")) {
    Write-Host "ARM GNU Toolchain already installed at $ToolsDir" -ForegroundColor Green
    exit 0
}

# ---- 查找本地缓存 ----
$CandidateDirs = @(
    # 同级 SDK 目录
    (Join-Path (Split-Path -Parent $ProjectRoot) "tm4c123gh6pm-sdk\tools"),
    # 当前目录
    $ToolsDir
)

$DownloadUrl = "https://developer.arm.com/-/media/Files/downloads/gnu/14.3.rel1/binrel/arm-gnu-toolchain-14.3.rel1-mingw-w64-x86_64-arm-none-eabi.zip"

foreach ($candidate in $CandidateDirs) {
    $gccPath = Join-Path $candidate "bin\arm-none-eabi-gcc.exe"
    Write-Host "  Checking: $gccPath" -ForegroundColor DarkGray
    if (Test-Path $gccPath) {
        Write-Host "Found cached toolchain at $candidate, copying..." -ForegroundColor Cyan
        Copy-Item $candidate $ToolsDir -Recurse -Force
        Write-Host "Done." -ForegroundColor Green
        exit 0
    }
}

# ---- 从 ARM 官方下载 ----
New-Item -ItemType Directory -Force -Path $ToolsDir, $DownloadsDir | Out-Null

$ZipName = "arm-gnu-toolchain-14.3.rel1-mingw-w64-x86_64-arm-none-eabi.zip"
$ZipPath = Join-Path $DownloadsDir $ZipName

# 如果已下载过但未解压，直接使用
if (-not (Test-Path $ZipPath)) {
    Write-Host "Downloading ARM GNU Toolchain (276 MB)..." -ForegroundColor Cyan
    Write-Host "  $DownloadUrl"
    Write-Host "  (this may take a few minutes depending on network speed)"

    $curl = Get-Command curl.exe -ErrorAction SilentlyContinue
    if ($curl) {
        & curl.exe -L --retry 3 --retry-delay 5 -o "$ZipPath" "$DownloadUrl"
    } else {
        Invoke-WebRequest -Uri $DownloadUrl -OutFile $ZipPath -UseBasicParsing
    }
}

Write-Host "Extracting to $ToolsDir ..." -ForegroundColor Cyan
Expand-Archive -Path $ZipPath -DestinationPath $ToolsDir -Force

Write-Host "ARM GNU Toolchain installed successfully." -ForegroundColor Green
& (Join-Path $ToolsDir "bin\arm-none-eabi-gcc.exe") --version
