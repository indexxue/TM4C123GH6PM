param([string]$Project="tm4c123-project")
$ErrorActionPreference="Stop"
$ProjectRoot=Split-Path -Parent $PSScriptRoot
$BuildDir=Join-Path $ProjectRoot "build"
$SrcDir=Join-Path $ProjectRoot "src"
$IncDir=Join-Path $ProjectRoot "include"
$LdScript=Join-Path $ProjectRoot "ld\tm4c123gh6pm.ld"
$ToolsDir=Join-Path $ProjectRoot "tools"
$ToolchainBin=Join-Path $ToolsDir "bin"

# ===== 1. ??????? =====================================================
$GccPath=Join-Path $ToolchainBin "arm-none-eabi-gcc.exe"
if(-not(Test-Path $GccPath)){
    Write-Host "ARM GNU Toolchain not found. Installing..." -ForegroundColor Yellow
    &(Join-Path $ProjectRoot "scripts\install-toolchain.ps1")
    if(-not(Test-Path $GccPath)){throw "Toolchain installation failed."}}
$env:PATH="$ToolchainBin;$env:PATH"
$gccVer=& arm-none-eabi-gcc --version
if($LASTEXITCODE-ne0){throw "arm-none-eabi-gcc not found after installation."}

# ===== 1b. ?? TivaWare =====================================================
$TivaWareRoot="D:\Ti\TivaWare_C_Series-2.2.0.295"
$TivaWareLib=Join-Path $TivaWareRoot "driverlib\gcc\libdriver.a"
$TivaWareInc=Join-Path $TivaWareRoot "inc"
$TivaWareFound=Test-Path $TivaWareLib

# ===== 2. ?? SysConfig =====================================================
&(Join-Path $ProjectRoot "scripts\run-sysconfig.ps1")
if($LASTEXITCODE-ne0){throw "SysConfig failed."}
New-Item -ItemType Directory -Force -Path $BuildDir|Out-Null

$TiDriversConfig=Join-Path $SrcDir "generated\ti_drivers_config.c"
$UseTivaWareDriver=$TivaWareFound -and (Test-Path $TiDriversConfig)

# ===== 3. ???? ===========================================================
$CommonFlags=@(
    "-mcpu=cortex-m4","-mthumb","-mfloat-abi=soft"
    "-DTM4C123GH6PM","-DPART_TM4C123GH6PM"
    "-I$IncDir","-I$SrcDir\generated"
    "-std=c11","-Wall","-Wextra","-Wpedantic"
    "-ffunction-sections","-fdata-sections","-Os","-g3"
)
$LinkFlags=@(
    "-mcpu=cortex-m4","-mthumb","-mfloat-abi=soft"
    "-T$LdScript"
    "-Wl,--gc-sections","-Wl,-Map=$BuildDir\$Project.map"
    "-nostartfiles","-specs=nosys.specs"
)
if($UseTivaWareDriver){
    $CommonFlags+="-I$TivaWareInc"
    $LinkFlags+="-L$(Join-Path $TivaWareRoot 'driverlib\gcc') -ldriver -lc -lgcc"
    Write-Host "TivaWare DriverLib: $TivaWareRoot" -ForegroundColor DarkGray
}

# ===== 4. ????? =========================================================
$Sources=@(
    (Join-Path $SrcDir "startup_tm4c123gh6pm.c")
    (Join-Path $SrcDir "main.c")
    (Join-Path $SrcDir "syscalls.c")
    (Join-Path $SrcDir "systick.c"), (Join-Path $SrcDir "generated\tm4c123_board.c")
)
if($UseTivaWareDriver){
    $Sources+=$TiDriversConfig
}
Write-Host "Compiling..." -ForegroundColor Cyan
$Objects=@()
foreach($Source in $Sources){
    $Object=Join-Path $BuildDir (([IO.Path]::GetFileNameWithoutExtension($Source))+".o")
    Write-Host "  $([IO.Path]::GetFileName($Source))" -ForegroundColor Gray
    & arm-none-eabi-gcc @CommonFlags -c $Source -o $Object
    if($LASTEXITCODE-ne0){throw "Compilation failed: $Source"}
    $Objects+=$Object
}

# ===== 5. ?? ===============================================================
$Elf=Join-Path $BuildDir "$Project.elf"
$Bin=Join-Path $BuildDir "$Project.bin"
$Map=Join-Path $BuildDir "$Project.map"
Write-Host "Linking..." -ForegroundColor Cyan
& arm-none-eabi-gcc @Objects @LinkFlags -o $Elf
if($LASTEXITCODE-ne0){throw "Link failed"}
& arm-none-eabi-objcopy -O binary $Elf $Bin
& arm-none-eabi-size $Elf
Write-Host "`nOutput:" -ForegroundColor Green
Write-Host "  $Elf`n  $Bin`n  $Map"
