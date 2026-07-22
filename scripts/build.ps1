param(
    [ValidateSet("car-4wd", "car-2wd")]
    [string]$CarProject = "car-4wd",

    [ValidateSet("standalone", "bootloader", "app", "factory", "all")]
    [string]$Target = "standalone",

    [ValidateSet("build", "clean", "rebuild", "release", "detect")]
    [string]$Action = "build",

    [string]$FwVersion = "",

    # Empty = derive from Action (release→0, else→1)
    [ValidateSet("", "0", "1")]
    [string]$LogEnable = ""
)

$ErrorActionPreference = "Stop"

$ProjectRoot = Split-Path -Parent $PSScriptRoot
$CarDir = Join-Path $ProjectRoot "projects\$CarProject"
$BuildDir = Join-Path $CarDir "build"
$MainDir = Join-Path $CarDir "main"
$BoardSrc = Join-Path $CarDir "board\src"
$BoardInc = Join-Path $CarDir "board\inc"
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
$ReleaseRoot = Join-Path $ProjectRoot "release"
$FwMcuName = if ($env:FW_MCU_NAME) { $env:FW_MCU_NAME } else { "TM4C123GH6PM" }

. (Join-Path $PSScriptRoot "sdk-path.ps1")

function Parse-SemverTag {
    param([string]$Raw)
    if ([string]::IsNullOrWhiteSpace($Raw)) { return $null }
    $t = $Raw.Trim()
    if ($t -match '^[vV]?(\d+)\.(\d+)\.(\d+)$') {
        return "$($Matches[1]).$($Matches[2]).$($Matches[3])"
    }
    return $null
}

function Get-GitSemverForBuild {
    try {
        $exact = (& git -C $ProjectRoot describe --exact-match --tags HEAD 2>$null)
        $v = Parse-SemverTag $exact
        if ($v) { return $v }
    } catch {}
    try {
        $near = (& git -C $ProjectRoot describe --tags --abbrev=0 2>$null)
        $v = Parse-SemverTag $near
        if ($v) { return $v }
    } catch {}
    return $null
}

function Get-GitMaxSemverTag {
    try {
        $tags = & git -C $ProjectRoot tag -l --sort=-version:refname 2>$null
        foreach ($t in $tags) {
            $v = Parse-SemverTag $t
            if ($v) { return $v }
        }
    } catch {}
    return $null
}

function Compare-SemverLt {
    param([string]$A, [string]$B)
    $aa = $A.Split('.') | ForEach-Object { [int]$_ }
    $bb = $B.Split('.') | ForEach-Object { [int]$_ }
    for ($i = 0; $i -lt 3; $i++) {
        if ($aa[$i] -lt $bb[$i]) { return $true }
        if ($aa[$i] -gt $bb[$i]) { return $false }
    }
    return $false
}

function Resolve-FwVersion {
    param([string]$Explicit, [switch]$ForRelease)
    if (-not [string]::IsNullOrWhiteSpace($Explicit)) {
        $parsed = Parse-SemverTag $Explicit
        if ($ForRelease) {
            if (-not $parsed) { throw "invalid -FwVersion '$Explicit' (need X.Y.Z)" }
            $floor = Get-GitMaxSemverTag
            if ($floor -and (Compare-SemverLt $parsed $floor)) {
                Write-Host "==> release $parsed below git tag v$floor — using $floor" -ForegroundColor Yellow
                return $floor
            }
            return $parsed
        }
        return $Explicit.Trim()
    }
    if ($env:FW_VERSION) {
        if ($ForRelease) {
            $parsed = Parse-SemverTag $env:FW_VERSION
            if (-not $parsed) { throw "invalid FW_VERSION '$($env:FW_VERSION)'" }
            return $parsed
        }
        return $env:FW_VERSION.Trim()
    }
    $gitVer = Get-GitSemverForBuild
    if ($gitVer) { return $gitVer }
    if ($ForRelease) {
        $max = Get-GitMaxSemverTag
        if ($max) { return $max }
        throw "release needs a version: pass -FwVersion 1.0.0 or set FW_VERSION / git tag"
    }
    return "0.0.0-dev"
}

function Get-ReleaseSignTag {
    switch -Regex ($env:FW_SIGNED) {
        '^(1|true|TRUE|yes|YES|on|ON)$' { return "sign" }
        default { return "unsigned" }
    }
}

function Get-IdentityDefines {
    param([string]$Version, [string]$Product, [string]$LogEn)
    $date = Get-Date -Format "yyyy-MM-dd"
    $time = Get-Date -Format "HH:mm:ss"
    # PowerShell strips quotes when invoking native exes; pass \" so gcc sees a C string.
    return @(
        ("-DFW_VERSION_STR=\`"{0}\`"" -f $Version),
        ("-DFW_PRODUCT_NAME=\`"{0}\`"" -f $Product),
        ("-DFW_BUILD_DATE=\`"{0}\`"" -f $date),
        ("-DFW_BUILD_TIME=\`"{0}\`"" -f $time),
        "-DLOG_ENABLE=$LogEn"
    )
}

function Show-Detect {
    Write-Host "Repo: $ProjectRoot"
    Write-Host "CarProject: $CarProject  Target: $Target  Action: $Action"
    $gcc = Join-Path $ToolchainBin "arm-none-eabi-gcc.exe"
    if (Test-Path $gcc) {
        Write-Host "gcc: $gcc"
        & $gcc --version | Select-Object -First 1
    } else {
        Write-Host "gcc: NOT FOUND (will install on first build)"
    }
    if (Test-TivaWareInstalled) {
        Write-Host "TivaWare: $Script:TivaWareRoot"
    } else {
        Write-Host "TivaWare: NOT FOUND (will install on first build)"
    }
    $py = Get-Command python -ErrorAction SilentlyContinue
    if ($py) { Write-Host "python: $($py.Source)" } else { Write-Host "python: NOT FOUND" }
}

function Clear-BuildDir {
    if (Test-Path $BuildDir) {
        Write-Host "Cleaning $BuildDir" -ForegroundColor Yellow
        Remove-Item -Recurse -Force $BuildDir
    } else {
        Write-Host "Nothing to clean: $BuildDir" -ForegroundColor DarkGray
    }
}

function Ensure-ToolchainReady {
    Ensure-TivaWare
    $script:TivaWareRoot = $Script:TivaWareRoot
    $script:TivaWareLib = $Script:TivaWareLib
    $script:TivaWareFound = Test-Path $Script:TivaWareLib
    $script:FreeRTOSRoot = $Script:FreeRTOSRoot
    $script:FreeRTOSPort = Join-Path $Script:FreeRTOSRoot "portable\GCC\ARM_CM4F"

    $GccPath = Join-Path $ToolchainBin "arm-none-eabi-gcc.exe"
    if (-not (Test-Path $GccPath)) {
        Write-Host "ARM GNU Toolchain not found. Installing..." -ForegroundColor Yellow
        & (Join-Path $ProjectRoot "scripts\install-toolchain.ps1")
        if (-not (Test-Path $GccPath)) { throw "Toolchain installation failed." }
    }

    $env:PATH = "$ToolchainBin;$env:PATH"
    & arm-none-eabi-gcc --version | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "arm-none-eabi-gcc not found." }
}

$GenConfig = Join-Path $ProjectRoot "scripts\gen_config.py"
$CheckSize = Join-Path $ProjectRoot "scripts\check-image-size.py"

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

    $Defines = @("-DTM4C123GH6PM", "-DPART_TM4C123GH6PM") + (Get-DeviceDefines) +
               $script:IdentityDefines + $ExtraDefines
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
    Write-Host "  FW_VERSION_STR=$($script:ResolvedFwVersion) LOG_ENABLE=$($script:ResolvedLogEnable)" -ForegroundColor DarkGray

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
    $Hex = Join-Path $BuildDir "$Name.hex"
    $Map = Join-Path $BuildDir "$Name.map"

    Write-Host "Linking $Name..." -ForegroundColor Cyan
    & arm-none-eabi-gcc @Objects @LinkFlags -o $Elf
    if ($LASTEXITCODE -ne 0) { throw "Link failed: $Name" }
    & arm-none-eabi-objcopy -O binary $Elf $Bin
    if ($LASTEXITCODE -ne 0) { throw "objcopy binary failed: $Name" }
    & arm-none-eabi-objcopy -O ihex $Elf $Hex
    if ($LASTEXITCODE -ne 0) { throw "objcopy ihex failed: $Name" }
    & arm-none-eabi-size $Elf

    if ($CheckImageSize) {
        & python $CheckSize $Bin --max $MaxImageSize
        if ($LASTEXITCODE -ne 0) { throw "Image size check failed: $Bin" }
    }

    Write-Host "Output:" -ForegroundColor Green
    Write-Host "  $Elf"
    Write-Host "  $Hex"
    Write-Host "  $Bin"
    Write-Host "  $Map"
}

function Invoke-FirmwareBuild {
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
}

function Get-PrimaryArtifactName {
    switch ($Target) {
        "standalone" { return $CarProject }
        "bootloader" { return "bootloader" }
        "app" { return "app" }
        "factory" { return "factory" }
        "all" { return $CarProject }
    }
}

function Publish-ReleaseArtifacts {
    param([string]$Version)

    $outDir = Join-Path $ReleaseRoot $Version
    New-Item -ItemType Directory -Force -Path $outDir | Out-Null
    $dateYmd = Get-Date -Format "yyyyMMdd"
    $sign = Get-ReleaseSignTag
    $stem = "${FwMcuName}_${dateYmd}_${CarProject}_${Version}_${sign}"

    $names = @()
    if ($Target -eq "all") {
        $names = @("bootloader", "app", "factory")
    } else {
        $names = @(Get-PrimaryArtifactName)
    }

    foreach ($name in $names) {
        $srcStem = Join-Path $BuildDir $name
        foreach ($ext in @("elf", "hex", "bin")) {
            $src = "$srcStem.$ext"
            if (-not (Test-Path $src)) { throw "missing artifact: $src" }
            if ($Target -eq "all") {
                $dest = Join-Path $outDir "${stem}_${name}.$ext"
            } else {
                $dest = Join-Path $outDir "$stem.$ext"
            }
            Copy-Item -Force $src $dest
            Write-Host "  packaged $dest" -ForegroundColor Green
        }
    }
}

# --- main ---
if ($Action -eq "detect") {
    Show-Detect
    exit 0
}

if ($Action -eq "clean") {
    Clear-BuildDir
    exit 0
}

$forRelease = ($Action -eq "release")
$script:ResolvedFwVersion = Resolve-FwVersion -Explicit $FwVersion -ForRelease:$forRelease
if ($LogEnable -ne "") {
    $script:ResolvedLogEnable = $LogEnable
} elseif ($forRelease) {
    $script:ResolvedLogEnable = "0"
} else {
    $script:ResolvedLogEnable = "1"
}
$script:IdentityDefines = Get-IdentityDefines -Version $script:ResolvedFwVersion -Product $CarProject -LogEn $script:ResolvedLogEnable

if ($Action -eq "rebuild" -or $Action -eq "release") {
    Clear-BuildDir
}

Ensure-ToolchainReady
New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null
Invoke-FirmwareBuild

if ($Action -eq "release") {
    Write-Host "=== Release package ver=$($script:ResolvedFwVersion) ===" -ForegroundColor Cyan
    Publish-ReleaseArtifacts -Version $script:ResolvedFwVersion
}
