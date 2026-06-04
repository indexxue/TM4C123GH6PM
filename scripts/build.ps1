param(
    [string]$Project = "tm4c123-project"
)

$ErrorActionPreference = "Stop"

# --- 路径 --------------------------------------------------------------------
$ProjectRoot  = Split-Path -Parent $PSScriptRoot
$BuildDir     = Join-Path $ProjectRoot "build"
$SrcDir       = Join-Path $ProjectRoot "src"
$IncDir       = Join-Path $ProjectRoot "include"

# SDK 路径 (同级目录)
$SdkRoot      = Join-Path (Split-Path -Parent $ProjectRoot) "tm4c123gh6pm-sdk"
$LdScript     = Join-Path $SdkRoot "ld\tm4c123gh6pm.ld"
$Startup      = Join-Path $SdkRoot "src\startup_tm4c123gh6pm.c"
$SdkInclude   = Join-Path $SdkRoot "include"
$ToolBin      = Join-Path $SdkRoot "tools\bin"

# 优先从 SDK 内查找工具链
if (-not (Get-Command arm-none-eabi-gcc -ErrorAction SilentlyContinue)) {
    $env:PATH = "$ToolBin;$env:PATH"
}

# 验证工具链
if (-not (Get-Command arm-none-eabi-gcc -ErrorAction SilentlyContinue)) {
    throw "arm-none-eabi-gcc not found. Expected at: $ToolBin"
}

New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null

# --- 编译标志 ----------------------------------------------------------------
$CommonFlags = @(
    "-mcpu=cortex-m4",
    "-mthumb",
    "-mfloat-abi=soft",
    "-DTM4C123GH6PM",
    "-DPART_TM4C123GH6PM",
    "-I$IncDir",
    "-I$SdkInclude",
    "-std=c11",
    "-Wall", "-Wextra", "-Wpedantic",
    "-ffunction-sections",
    "-fdata-sections",
    "-Os",
    "-g3"
)

# --- 编译启动文件 (来自 SDK) ---------------------------------------------------
Write-Host "Compiling startup (SDK)..." -ForegroundColor Cyan
& arm-none-eabi-gcc @CommonFlags -c $Startup -o (Join-Path $BuildDir "startup_tm4c123gh6pm.o")

# --- 编译项目源文件 ------------------------------------------------------------
$Sources = @(
    (Join-Path $SrcDir "main.c"),
    (Join-Path $SrcDir "syscalls.c"),
    (Join-Path $SrcDir "systick.c")
)

$Objects = @()
foreach ($Source in $Sources) {
    $Object = Join-Path $BuildDir (([IO.Path]::GetFileNameWithoutExtension($Source)) + ".o")
    Write-Host "  $($Source)" -ForegroundColor Gray
    & arm-none-eabi-gcc @CommonFlags -c $Source -o $Object
    $Objects += $Object
}

# --- 链接 --------------------------------------------------------------------
$Elf = Join-Path $BuildDir "$Project.elf"
$Bin = Join-Path $BuildDir "$Project.bin"
$Map = Join-Path $BuildDir "$Project.map"

$LinkFlags = @(
    "-mcpu=cortex-m4",
    "-mthumb",
    "-mfloat-abi=soft",
    "-T$LdScript",
    "-Wl,--gc-sections",
    "-Wl,-Map=$Map",
    "-nostartfiles",
    "-specs=nosys.specs"
)

Write-Host "Linking..." -ForegroundColor Cyan
& arm-none-eabi-gcc @(Join-Path $BuildDir "startup_tm4c123gh6pm.o") @Objects @LinkFlags -o $Elf
if ($LASTEXITCODE -ne 0) { throw "Link failed" }

& arm-none-eabi-objcopy -O binary $Elf $Bin
& arm-none-eabi-size $Elf

Write-Host "`nOutput files:" -ForegroundColor Green
Write-Host "  $Elf"
Write-Host "  $Bin"
Write-Host "  $Map"
