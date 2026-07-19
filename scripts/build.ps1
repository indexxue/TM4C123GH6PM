param(
    [ValidateSet("car-4wd", "car-2wd")]
    [string]$CarProject = "car-4wd",

    [ValidateSet("standalone", "bootloader", "app", "factory", "all")]
    [string]$Target = "standalone"
)

$ErrorActionPreference = "Stop"

$ProjectRoot = Split-Path -Parent $PSScriptRoot
$CarDir = Join-Path $ProjectRoot "projects\$CarProject"
$BuildDir = Join-Path $CarDir "build"
$MainDir = Join-Path $CarDir "main"
$BoardSrc = Join-Path $CarDir "board\src"
$BoardInc = Join-Path $CarDir "board\inc"
$SyscfgManifest = Join-Path $CarDir ".syscfg\project.json"
$BlDir = Join-Path $ProjectRoot "bootloader"
$FactoryDir = Join-Path $ProjectRoot "factory"
$IncDir = Join-Path $ProjectRoot "include"
$CommonDir = Join-Path $ProjectRoot "Common"
$CommonInc = Join-Path $CommonDir "inc"
$CommonSrc = Join-Path $CommonDir "src"
$BspInc = Join-Path $ProjectRoot "bsp_driver\inc"
$BspSrc = Join-Path $ProjectRoot "bsp_driver\src"
$CbbDir = Join-Path $ProjectRoot "cbb"
$ThirdPartyDir = Join-Path $ProjectRoot "third_party"
$LdDir = Join-Path $ProjectRoot "ld"
$ToolsDir = Join-Path $ProjectRoot "tools"
$ToolchainBin = Join-Path $ToolsDir "bin"
. (Join-Path $PSScriptRoot "sdk-path.ps1")
Ensure-TivaWare
$TivaWareRoot = $Script:TivaWareRoot
$TivaWareLib = $Script:TivaWareLib
$TivaWareFound = Test-Path $TivaWareLib
$FreeRTOSRoot = $Script:FreeRTOSRoot
$FreeRTOSPort = Join-Path $FreeRTOSRoot "portable\GCC\ARM_CM4F"
$GenConfig = Join-Path $ProjectRoot "scripts\gen_config.py"
$CheckSize = Join-Path $ProjectRoot "scripts\check-image-size.py"


$GccPath = Join-Path $ToolchainBin "arm-none-eabi-gcc.exe"
if (-not (Test-Path $GccPath)) {
    Write-Host "ARM GNU Toolchain not found. Installing..." -ForegroundColor Yellow
    & (Join-Path $ProjectRoot "scripts\install-toolchain.ps1")
    if (-not (Test-Path $GccPath)) { throw "Toolchain installation failed." }
}

$env:PATH = "$ToolchainBin;$env:PATH"
& arm-none-eabi-gcc --version | Out-Null
if ($LASTEXITCODE -ne 0) { throw "arm-none-eabi-gcc not found." }

function Get-DeviceDefines {
    $productId = if ($CarProject -eq "car-2wd") { 2 } else { 1 }
    return @("-DDEVICE_PRODUCT_ID=$productId")
}

function Get-NvsAppDefines {
    return @("-DNVS_RTOS_LOCK", "-DNVS_CMD_RAW_KV")
}

function Get-CarAppDefines {
    return @()
}

function Get-CbbSources {
    return @(
        (Join-Path $CbbDir "qmc5883p\qmc5883p.c"),
        (Join-Path $CbbDir "mpu6050\mpu6050.c"),
        (Join-Path $CbbDir "ws2812b\ws2812b.c"),
        (Join-Path $CbbDir "hc_sr04\hc_sr04.c")
    )
}

function Get-CbbIncludes {
    return @(
        (Join-Path $CbbDir "qmc5883p"),
        (Join-Path $CbbDir "mpu6050"),
        (Join-Path $CbbDir "ws2812b"),
        (Join-Path $CbbDir "hc_sr04")
    )
}

function Get-ThirdPartySources {
    return @(
        (Join-Path $ThirdPartyDir "Fusion\Fusion\FusionAhrs.c"),
        (Join-Path $ThirdPartyDir "Fusion\Fusion\FusionBias.c"),
        (Join-Path $ThirdPartyDir "Fusion\Fusion\FusionCompass.c"),
        (Join-Path $ThirdPartyDir "pid\pid.c")
    )
}

function Get-ThirdPartyIncludes {
    return @(
        (Join-Path $ThirdPartyDir "Fusion\Fusion"),
        (Join-Path $ThirdPartyDir "pid")
    )
}

function Get-BspSources {
    return @(
        (Join-Path $BspSrc "bsp_sysctl.c"),
        (Join-Path $BspSrc "bsp_gpio.c"),
        (Join-Path $BspSrc "bsp_systick.c"),
        (Join-Path $BspSrc "bsp_uart.c"),
        (Join-Path $BspSrc "bsp_i2c.c"),
        (Join-Path $BspSrc "bsp_adc.c"),
        (Join-Path $BspSrc "bsp_timer.c"),
        (Join-Path $BspSrc "bsp_qei.c"),
        (Join-Path $BspSrc "bsp_sw_qei.c"),
        (Join-Path $BspSrc "bsp_dma.c"),
        (Join-Path $BspSrc "bsp_spi.c"),
        (Join-Path $BspSrc "bsp_dac.c"),
        (Join-Path $BspSrc "bsp_bus_lock.c")
    )
}

function Get-GeneratedBoardSources {
    return "motor.c", "encoder.c", "line.c", "board.c" | ForEach-Object {
        Join-Path $BoardSrc $_
    }
}

function Get-FreeRtosSources {
    return @(
        (Join-Path $FreeRTOSRoot "tasks.c"),
        (Join-Path $FreeRTOSRoot "queue.c"),
        (Join-Path $FreeRTOSRoot "list.c"),
        (Join-Path $FreeRTOSRoot "timers.c"),
        (Join-Path $FreeRTOSPort "port.c"),
        (Join-Path $FreeRTOSRoot "portable\MemMang\heap_4.c")
    )
}

function Get-CommonCoreSources {
    return @(
        (Join-Path $CommonSrc "device_profile.c"),
        (Join-Path $CommonSrc "start.c"),
        (Join-Path $CommonSrc "event.c"),
        (Join-Path $CommonSrc "log.c")
    )
}

function Get-FullCommonSources {
    return (Get-CommonCoreSources) + @(
        (Join-Path $CommonSrc "battery.c"),
        (Join-Path $CommonSrc "button.c"),
        (Join-Path $CommonSrc "buzzer.c"),
        (Join-Path $CommonSrc "flexible_button.c"),
        (Join-Path $CommonSrc "crc32.c"),
        (Join-Path $CommonSrc "nvs.c"),
        (Join-Path $CommonSrc "cfg.c"),
        (Join-Path $CommonSrc "imu.c"),
        (Join-Path $CommonSrc "magnetometer.c"),
        (Join-Path $CommonSrc "ultrasonic.c"),
        (Join-Path $CommonSrc "attitude.c"),
        (Join-Path $CommonSrc "motion.c"),
        (Join-Path $CommonSrc "line_follow.c"),
        (Join-Path $CommonSrc "chassis.c"),
        (Join-Path $CommonSrc "proto.c"),
        (Join-Path $CommonSrc "led_scene.c")
    )
}

function Get-FactoryCommonSources {
    return (Get-FullCommonSources) + @(
        (Join-Path $CommonSrc "cmd.c")
    )
}

function Get-MainSources {
    return @(
        (Join-Path $MainDir "startup_tm4c123gh6pm.c"),
        (Join-Path $MainDir "main.c"),
        (Join-Path $MainDir "app.c"),
        (Join-Path $MainDir "freertos_hooks.c"),
        (Join-Path $MainDir "syscalls.c")
    )
}

function Get-AppSources {
    return (Get-MainSources) + (Get-BspSources) + (Get-CbbSources) + (Get-ThirdPartySources) +
           (Get-FullCommonSources) + (Get-GeneratedBoardSources) + (Get-FreeRtosSources)
}

function Get-FactorySources {
    return @(
        (Join-Path $MainDir "startup_tm4c123gh6pm.c"),
        (Join-Path $MainDir "freertos_hooks.c"),
        (Join-Path $MainDir "syscalls.c"),
        (Join-Path $FactoryDir "main.c"),
        (Join-Path $FactoryDir "factory.c")
    ) + (Get-BspSources) + (Get-CbbSources) + (Get-ThirdPartySources) + (Get-FactoryCommonSources) +
        (Get-GeneratedBoardSources) + (Get-FreeRtosSources)
}

function Get-BootloaderSources {
    return @(
        (Join-Path $BlDir "bootloader.c")
    )
}

function Invoke-AppCodegen {
    param([switch]$CompileDb)

    $codegenArgs = @($GenConfig, "--car-project", $CarProject)
    if ($CompileDb) { $codegenArgs += "--ide-db" }
    & python @codegenArgs
    if ($LASTEXITCODE -ne 0) { throw "gen_config.py failed." }
}

function Build-FirmwareTarget {
    param(
        [string]$Name,
        [string]$LdScript,
        [string[]]$Sources,
        [string[]]$ExtraDefines = @(),
        [string[]]$ExtraIncludes = @(),
        [switch]$CheckImageSize,
        [int]$MaxImageSize = (116 * 1024)
    )

    $ObjDir = Join-Path $BuildDir "obj\$Name"
    New-Item -ItemType Directory -Force -Path $ObjDir | Out-Null

    $Defines = @("-DTM4C123GH6PM", "-DPART_TM4C123GH6PM") + (Get-DeviceDefines) + $ExtraDefines
    $Includes = @(
        "-I$IncDir",
        "-I$BoardInc",
        "-I$BspInc",
        "-I$CommonInc",
        "-I$BlDir",
        "-I$FreeRTOSRoot\include",
        "-I$FreeRTOSPort"
    ) + (Get-CbbIncludes | ForEach-Object { "-I$_" }) +
        (Get-ThirdPartyIncludes | ForEach-Object { "-I$_" }) + ($ExtraIncludes | ForEach-Object { "-I$_" })

    $CommonFlags = @(
        "-mcpu=cortex-m4", "-mthumb", "-mfloat-abi=hard", "-mfpu=fpv4-sp-d16"
    ) + $Defines + $Includes + @(
        "-std=c11", "-Wall", "-Wextra", "-Wpedantic",
        "-ffunction-sections", "-fdata-sections", "-Os", "-g3"
    )

    $LinkFlags = @(
        "-mcpu=cortex-m4", "-mthumb", "-mfloat-abi=hard", "-mfpu=fpv4-sp-d16",
        "-T$LdScript",
        "-Wl,--gc-sections", "-Wl,-Map=$BuildDir\$Name.map",
        "-nostartfiles", "-specs=nosys.specs", "-specs=nano.specs"
    )

    if ($TivaWareFound) {
        $CommonFlags += "-I$TivaWareRoot"
        $CommonFlags += "-I$(Join-Path $TivaWareRoot "inc")"
        $LinkFlags += "-L$(Join-Path $TivaWareRoot "driverlib\gcc")"
        $LinkFlags += "-ldriver", "-lc", "-lm", "-lgcc"
    }

    Write-Host "=== Target: $Name ===" -ForegroundColor Cyan
    Write-Host "  LD: $LdScript" -ForegroundColor DarkGray

    $Objects = @()
    foreach ($Source in $Sources) {
        $AbsSource = (Resolve-Path -LiteralPath $Source).Path
        $Rel = $AbsSource.Substring($ProjectRoot.Length).TrimStart('\', '/')
        $Safe = ($Rel -replace '[\\/]', '_') -replace '\.c$','.o'
        $Object = Join-Path $ObjDir $Safe
        Write-Host "  $Rel" -ForegroundColor Gray
        & arm-none-eabi-gcc @CommonFlags -c $AbsSource -o $Object
        if ($LASTEXITCODE -ne 0) { throw "Compilation failed: $Source" }
        $Objects += $Object
    }

    $Elf = Join-Path $BuildDir "$Name.elf"
    $Bin = Join-Path $BuildDir "$Name.bin"
    $Map = Join-Path $BuildDir "$Name.map"

    Write-Host "Linking $Name..." -ForegroundColor Cyan
    & arm-none-eabi-gcc @Objects @LinkFlags -o $Elf
    if ($LASTEXITCODE -ne 0) { throw "Link failed: $Name" }
    & arm-none-eabi-objcopy -O binary $Elf $Bin
    & arm-none-eabi-size $Elf

    if ($CheckImageSize) {
        & python $CheckSize $Bin --max $MaxImageSize
        if ($LASTEXITCODE -ne 0) { throw "Image size check failed: $Bin" }
    }

    Write-Host "Output:" -ForegroundColor Green
    Write-Host "  $Elf"
    Write-Host "  $Bin"
    Write-Host "  $Map"
}

New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null

switch ($Target) {
    "standalone" {
        Invoke-AppCodegen -CompileDb
        Build-FirmwareTarget -Name $CarProject -LdScript (Join-Path $LdDir "tm4c123gh6pm.ld") -Sources (Get-AppSources) -ExtraDefines ((Get-NvsAppDefines) + (Get-CarAppDefines))
    }
    "bootloader" {
        Build-FirmwareTarget -Name "bootloader" -LdScript (Join-Path $LdDir "bootloader.ld") -Sources (Get-BootloaderSources) -CheckImageSize -MaxImageSize (16 * 1024)
    }
    "app" {
        Invoke-AppCodegen
        Build-FirmwareTarget -Name "app" -LdScript (Join-Path $LdDir "app.ld") -Sources (Get-AppSources) -ExtraDefines (@("-DFLASH_APP_A_SLOT") + (Get-NvsAppDefines) + (Get-CarAppDefines)) -CheckImageSize
    }
    "factory" {
        Invoke-AppCodegen
        Build-FirmwareTarget -Name "factory" -LdScript (Join-Path $LdDir "factory.ld") -Sources (Get-FactorySources) -ExtraDefines (@("-DFLASH_FACTORY_SLOT") + (Get-NvsAppDefines)) -ExtraIncludes @($FactoryDir) -CheckImageSize
    }
    "all" {
        Invoke-AppCodegen
        Build-FirmwareTarget -Name "bootloader" -LdScript (Join-Path $LdDir "bootloader.ld") -Sources (Get-BootloaderSources) -CheckImageSize -MaxImageSize (16 * 1024)
        Build-FirmwareTarget -Name "app" -LdScript (Join-Path $LdDir "app.ld") -Sources (Get-AppSources) -ExtraDefines (@("-DFLASH_APP_A_SLOT") + (Get-NvsAppDefines) + (Get-CarAppDefines)) -CheckImageSize
        Build-FirmwareTarget -Name "factory" -LdScript (Join-Path $LdDir "factory.ld") -Sources (Get-FactorySources) -ExtraDefines (@("-DFLASH_FACTORY_SLOT") + (Get-NvsAppDefines)) -ExtraIncludes @($FactoryDir) -CheckImageSize
    }
}
