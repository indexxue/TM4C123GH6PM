param(

    [string]$Project = "tm4c123-project",

    [ValidateSet("standalone", "bootloader", "app", "factory", "all")]

    [string]$Target = "standalone"

)

$ErrorActionPreference = "Stop"

$ProjectRoot = Split-Path -Parent $PSScriptRoot

$BuildDir = Join-Path $ProjectRoot "build"

$SrcDir = Join-Path $ProjectRoot "src"

$BlDir = Join-Path $ProjectRoot "bootloader"

$FactoryDir = Join-Path $ProjectRoot "factory"

$IncDir = Join-Path $ProjectRoot "include"

$CommonDir = Join-Path $ProjectRoot "Common"

$CommonInc = Join-Path $CommonDir "inc"

$CommonSrc = Join-Path $CommonDir "src"

$CbbWs2812 = Join-Path $ProjectRoot "cbb\ws2812b"

$LdDir = Join-Path $ProjectRoot "ld"

$ToolsDir = Join-Path $ProjectRoot "tools"

$ToolchainBin = Join-Path $ToolsDir "bin"

$TivaWareRoot = "D:\Ti\TivaWare_C_Series-2.2.0.295"

$TivaWareLib = Join-Path $TivaWareRoot "driverlib\gcc\libdriver.a"

$TivaWareFound = Test-Path $TivaWareLib

$FreeRTOSRoot = Join-Path $TivaWareRoot "third_party\FreeRTOS\Source"

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



if (-not (Test-Path (Join-Path $FreeRTOSRoot "tasks.c"))) {

    throw "FreeRTOS not found at $FreeRTOSRoot (install TivaWare C Series)."

}



function Get-GeneratedBoardSources {

    return "motor.c", "encoder.c", "line.c", "board.c" | ForEach-Object {

        Join-Path $CommonSrc $_

    }

}



function Get-SharedAppSources {

    return @(

        (Join-Path $CommonSrc "type.c"),

        (Join-Path $CommonSrc "log.c"),

        (Join-Path $CommonSrc "cmd.c"),

        (Join-Path $CommonSrc "battery.c"),

        (Join-Path $CommonSrc "button.c"),

        (Join-Path $CommonSrc "flexible_button.c"),

        (Join-Path $CommonSrc "led_scene.c"),

        (Join-Path $CommonSrc "crc32.c"),

        (Join-Path $CommonSrc "nvs_flash_ops.c"),

        (Join-Path $CommonSrc "nvs.c"),

        (Join-Path $CommonSrc "ota_meta.c"),

        (Join-Path $CbbWs2812 "ws2812b.c"),

        (Join-Path $FreeRTOSRoot "tasks.c"),

        (Join-Path $FreeRTOSRoot "queue.c"),

        (Join-Path $FreeRTOSRoot "list.c"),

        (Join-Path $FreeRTOSPort "port.c"),

        (Join-Path $FreeRTOSRoot "portable\MemMang\heap_4.c")

    )

}



function Invoke-AppCodegen {

    param([switch]$CompileDb)



    $args = @($GenConfig)

    if ($CompileDb) { $args += "--ide-db" }

    & python @args

    if ($LASTEXITCODE -ne 0) { throw "gen_config.py failed." }

}



function Get-AppSources {

    return @(

        (Join-Path $SrcDir "startup_tm4c123gh6pm.c"),

        (Join-Path $SrcDir "main.c"),

        (Join-Path $SrcDir "init.c"),

        (Join-Path $SrcDir "app.c"),

        (Join-Path $SrcDir "freertos_hooks.c"),

        (Join-Path $SrcDir "syscalls.c")

    ) + (Get-GeneratedBoardSources) + (Get-SharedAppSources)

}



function Get-FactorySources {

    return @(

        (Join-Path $SrcDir "startup_tm4c123gh6pm.c"),

        (Join-Path $FactoryDir "factory_main.c"),

        (Join-Path $FactoryDir "factory_init.c"),

        (Join-Path $FactoryDir "factory_app.c"),

        (Join-Path $SrcDir "freertos_hooks.c"),

        (Join-Path $SrcDir "syscalls.c")

    ) + (Get-GeneratedBoardSources) + (Get-SharedAppSources)

}



function Get-BootloaderSources {

    return @(

        (Join-Path $BlDir "bootloader.c"),

        (Join-Path $CommonSrc "crc32.c")

    )

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



    $Defines = @("-DTM4C123GH6PM", "-DPART_TM4C123GH6PM") + $ExtraDefines

    $Includes = @(

        "-I$IncDir", "-I$CommonInc", "-I$CbbWs2812", "-I$BlDir",

        "-I$FreeRTOSRoot\include", "-I$FreeRTOSPort"

    ) + ($ExtraIncludes | ForEach-Object { "-I$_" })



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

        "-nostartfiles", "-specs=nosys.specs"

    )



    if ($TivaWareFound) {

        $CommonFlags += "-I$TivaWareRoot"

        $CommonFlags += "-I$(Join-Path $TivaWareRoot "inc")"

        $LinkFlags += "-L$(Join-Path $TivaWareRoot "driverlib\gcc")"

        $LinkFlags += "-ldriver", "-lc", "-lgcc"

    }



    Write-Host "=== Target: $Name ===" -ForegroundColor Cyan

    Write-Host "  LD: $LdScript" -ForegroundColor DarkGray



    $Objects = @()

    foreach ($Source in $Sources) {

        $Base = [IO.Path]::GetFileNameWithoutExtension($Source)

        $Object = Join-Path $ObjDir "$Base.o"

        Write-Host "  $([IO.Path]::GetFileName($Source))" -ForegroundColor Gray

        & arm-none-eabi-gcc @CommonFlags -c $Source -o $Object

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

        Build-FirmwareTarget -Name $Project -LdScript (Join-Path $LdDir "tm4c123gh6pm.ld") -Sources (Get-AppSources)

    }

    "bootloader" {

        Build-FirmwareTarget -Name "bootloader" -LdScript (Join-Path $LdDir "bootloader.ld") -Sources (Get-BootloaderSources) -CheckImageSize -MaxImageSize (16 * 1024)

    }

    "app" {

        Invoke-AppCodegen

        Build-FirmwareTarget -Name "app" -LdScript (Join-Path $LdDir "app.ld") -Sources (Get-AppSources) -ExtraDefines @("-DFLASH_APP_A_SLOT") -CheckImageSize

    }

    "factory" {

        Invoke-AppCodegen

        Build-FirmwareTarget -Name "factory" -LdScript (Join-Path $LdDir "factory.ld") -Sources (Get-FactorySources) -ExtraDefines @("-DFLASH_FACTORY_SLOT") -ExtraIncludes @($FactoryDir) -CheckImageSize

    }

    "all" {

        Invoke-AppCodegen

        Build-FirmwareTarget -Name "bootloader" -LdScript (Join-Path $LdDir "bootloader.ld") -Sources (Get-BootloaderSources) -CheckImageSize -MaxImageSize (16 * 1024)

        Build-FirmwareTarget -Name "app" -LdScript (Join-Path $LdDir "app.ld") -Sources (Get-AppSources) -ExtraDefines @("-DFLASH_APP_A_SLOT") -CheckImageSize

        Build-FirmwareTarget -Name "factory" -LdScript (Join-Path $LdDir "factory.ld") -Sources (Get-FactorySources) -ExtraDefines @("-DFLASH_FACTORY_SLOT") -ExtraIncludes @($FactoryDir) -CheckImageSize

    }

}

