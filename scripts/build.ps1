param(
    [string]$Project = "tm4c123-project"
)

$ErrorActionPreference = "Stop"

$ProjectRoot  = Split-Path -Parent $PSScriptRoot
$BuildDir     = Join-Path $ProjectRoot "build"
$SrcDir       = Join-Path $ProjectRoot "src"
$IncDir       = Join-Path $ProjectRoot "include"
$LdScript     = Join-Path $ProjectRoot "ld\tm4c123gh6pm.ld"
$ToolsDir     = Join-Path $ProjectRoot "tools"
$ToolchainBin = Join-Path $ToolsDir "bin"

# ===== 1. 自动安装工具链 =====================================================
$GccPath = Join-Path $ToolchainBin "arm-none-eabi-gcc.exe"
if (-not (Test-Path $GccPath)) {
    Write-Host "ARM GNU Toolchain not found. Installing..." -ForegroundColor Yellow
    & (Join-Path $ProjectRoot "scripts\install-toolchain.ps1")
    if (-not (Test-Path $GccPath)) {
        throw "Toolchain installation failed."
    }
}

# 加入 PATH
$env:PATH = "$ToolchainBin;$env:PATH"

# 验证
$gccVer = & arm-none-eabi-gcc --version
if ($LASTEXITCODE -ne 0) { throw "arm-none-eabi-gcc not found after installation." }

# ===== 2. 创建构建目录 =======================================================
New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null

# ===== 3. 编译标志 ===========================================================
$CommonFlags = @(
    "-mcpu=cortex-m4",
    "-mthumb",
    "-mfloat-abi=soft",
    "-DTM4C123GH6PM",
    "-DPART_TM4C123GH6PM",
    "-I$IncDir",
    "-std=c11",
    "-Wall", "-Wextra", "-Wpedantic",
    "-ffunction-sections",
    "-fdata-sections",
    "-Os",
    "-g3"
)

# ===== 4. 编译所有源文件 =====================================================
$Sources = @(
    (Join-Path $SrcDir "startup_tm4c123gh6pm.c"),
    (Join-Path $SrcDir "main.c"),
    (Join-Path $SrcDir "syscalls.c"),
    (Join-Path $SrcDir "systick.c")
)

Write-Host "Compiling..." -ForegroundColor Cyan

$Objects = @()
foreach ($Source in $Sources) {
    $Object = Join-Path $BuildDir (([IO.Path]::GetFileNameWithoutExtension($Source)) + ".o")
    Write-Host "  $([IO.Path]::GetFileName($Source))" -ForegroundColor Gray
    & arm-none-eabi-gcc @CommonFlags -c $Source -o $Object
    if ($LASTEXITCODE -ne 0) { throw "Compilation failed: $Source" }
    $Objects += $Object
}

# ===== 5. 链接 ===============================================================
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
& arm-none-eabi-gcc @Objects @LinkFlags -o $Elf
if ($LASTEXITCODE -ne 0) { throw "Link failed" }

& arm-none-eabi-objcopy -O binary $Elf $Bin
& arm-none-eabi-size $Elf

Write-Host "`nOutput files:" -ForegroundColor Green
Write-Host "  $Elf"
Write-Host "  $Bin"
Write-Host "  $Map"
