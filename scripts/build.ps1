param([string]$Project="tm4c123-project")
$ErrorActionPreference="Stop"
$ProjectRoot=Split-Path -Parent $PSScriptRoot
$BuildDir=Join-Path $ProjectRoot "build"
$SrcDir=Join-Path $ProjectRoot "src"
$IncDir=Join-Path $ProjectRoot "include"
$LdScript=Join-Path $ProjectRoot "ld\tm4c123gh6pm.ld"
$ToolsDir=Join-Path $ProjectRoot "tools"
$ToolchainBin=Join-Path $ToolsDir "bin"
$TivaWareRoot="D:\Ti\TivaWare_C_Series-2.2.0.295"
$TivaWareLib=Join-Path $TivaWareRoot "driverlib\gcc\libdriver.a"
$TivaWareFound=Test-Path $TivaWareLib
$FreeRTOSRoot=Join-Path $TivaWareRoot "third_party\FreeRTOS\Source"
$FreeRTOSPort=Join-Path $FreeRTOSRoot "portable\GCC\ARM_CM4F"

$GccPath=Join-Path $ToolchainBin "arm-none-eabi-gcc.exe"
if(-not(Test-Path $GccPath)){
    Write-Host "ARM GNU Toolchain not found. Installing..." -ForegroundColor Yellow
    &(Join-Path $ProjectRoot "scripts\install-toolchain.ps1")
    if(-not(Test-Path $GccPath)){throw "Toolchain installation failed."}}
$env:PATH="$ToolchainBin;$env:PATH"
$gccVer=& arm-none-eabi-gcc --version
if($LASTEXITCODE-ne0){throw "arm-none-eabi-gcc not found."}

if(-not(Test-Path (Join-Path $FreeRTOSRoot "tasks.c"))){
    throw "FreeRTOS not found at $FreeRTOSRoot (install TivaWare C Series)."
}

&(Join-Path $ProjectRoot "scripts\run-sysconfig.ps1")
if($LASTEXITCODE-ne0){throw "SysConfig failed."}
& python (Join-Path $ProjectRoot "scripts\gen-board-config.py")
& python (Join-Path $ProjectRoot "scripts\gen-car-config.py")
New-Item -ItemType Directory -Force -Path $BuildDir|Out-Null

$CommonFlags=@(
    "-mcpu=cortex-m4","-mthumb","-mfloat-abi=hard","-mfpu=fpv4-sp-d16"
    "-DTM4C123GH6PM","-DPART_TM4C123GH6PM"
    "-I$IncDir","-I$SrcDir\generated"
    "-I$FreeRTOSRoot\include"
    "-I$FreeRTOSPort"
    "-std=c11","-Wall","-Wextra","-Wpedantic"
    "-ffunction-sections","-fdata-sections","-Os","-g3"
)
$LinkFlags=@(
    "-mcpu=cortex-m4","-mthumb","-mfloat-abi=hard","-mfpu=fpv4-sp-d16"
    "-T$LdScript"
    "-Wl,--gc-sections","-Wl,-Map=$BuildDir\$Project.map"
    "-nostartfiles","-specs=nosys.specs"
)
if($TivaWareFound){
    Write-Host "TivaWare: $TivaWareRoot" -ForegroundColor DarkGray
    $CommonFlags+="-I$TivaWareRoot"
    $CommonFlags+="-I$(Join-Path $TivaWareRoot "inc")"
    $LinkFlags+="-L$(Join-Path $TivaWareRoot "driverlib\gcc")"
    $LinkFlags+="-ldriver"
    $LinkFlags+="-lc"
    $LinkFlags+="-lgcc"
}

$Sources=@(
    (Join-Path $SrcDir "startup_tm4c123gh6pm.c")
    (Join-Path $SrcDir "main.c")
    (Join-Path $SrcDir "app_tasks.c")
    (Join-Path $SrcDir "freertos_hooks.c")
    (Join-Path $SrcDir "syscalls.c")
    (Join-Path $SrcDir "generated\pinout.c")
    (Join-Path $SrcDir "generated\car_config.c")
    (Join-Path $FreeRTOSRoot "tasks.c")
    (Join-Path $FreeRTOSRoot "queue.c")
    (Join-Path $FreeRTOSRoot "list.c")
    (Join-Path $FreeRTOSPort "port.c")
    (Join-Path $FreeRTOSRoot "portable\MemMang\heap_4.c")
)

Write-Host "FreeRTOS: $FreeRTOSRoot" -ForegroundColor DarkGray
Write-Host "Compiling..." -ForegroundColor Cyan
$Objects=@()
foreach($Source in $Sources){
    $Object=Join-Path $BuildDir (([IO.Path]::GetFileNameWithoutExtension($Source))+".o")
    Write-Host "  $([IO.Path]::GetFileName($Source))" -ForegroundColor Gray
    & arm-none-eabi-gcc @CommonFlags -c $Source -o $Object
    if($LASTEXITCODE-ne0){throw "Compilation failed: $Source"}
    $Objects+=$Object
}
Write-Host "Linking..." -ForegroundColor Cyan
$Elf=Join-Path $BuildDir "$Project.elf"
$Bin=Join-Path $BuildDir "$Project.bin"
$Map=Join-Path $BuildDir "$Project.map"
& arm-none-eabi-gcc @Objects @LinkFlags -o $Elf
if($LASTEXITCODE-ne0){throw "Link failed"}
& arm-none-eabi-objcopy -O binary $Elf $Bin
& arm-none-eabi-size $Elf

& python (Join-Path $ProjectRoot "scripts\gen-compile-commands.py")

Write-Host "Output:" -ForegroundColor Green
Write-Host "  $Elf`n  $Bin`n  $Map"
