param(
    [string]$Image = "",
    [ValidateSet("standalone", "bootloader", "app", "factory", "full")]
    [string]$Target = "standalone",
    [ValidateSet("factory", "car-4wd", "car-2wd", "rc-controller")]
    [string]$CarProject = "car-4wd",
    [switch]$EraseAll,
    [switch]$EraseApps,
    [switch]$Recover,
    [int]$Speed = 400
)
$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent $PSScriptRoot

function Test-JLinkSharedToWsl {
    $usbipdCandidates = @(
        "${env:ProgramFiles}\usbipd-win\usbipd.exe",
        "${env:ProgramFiles(x86)}\usbipd-win\usbipd.exe"
    )
    foreach ($usbipd in $usbipdCandidates) {
        if (-not (Test-Path $usbipd)) { continue }
        $list = & $usbipd list 2>&1 | Out-String
        if ($list -match '1366:0105[^\r\n]*Shared') {
            Write-Host ""
            Write-Host "J-Link is attached to WSL (usbipd Shared). Windows J-Link cannot connect." -ForegroundColor Yellow
            Write-Host "  WSL flash (preferred):  cd projects && ./flash.sh $CarProject" -ForegroundColor Cyan
            Write-Host "  Or detach for Windows:  usbipd detach --busid <BUSID>" -ForegroundColor DarkGray
            Write-Host ""
            return $true
        }
    }
    return $false
}

# factory image: look under the selected car project's build/
#   .\flash-jlink.cmd -Target factory
#   .\flash-jlink.cmd -Target factory -CarProject car-2wd
#   .\flash-jlink.cmd -Target full -CarProject car-4wd
# Missing build artifacts: auto-build that Target (LOG_ENABLE=1) then flash.

$BuildDir = Join-Path $ProjectRoot "projects\$CarProject\build"
$JLinkExe = "C:\Program Files\SEGGER\JLink_V818\JLink.exe"
$BuildPs1 = Join-Path $PSScriptRoot "build.ps1"

if (($Target -eq "full") -and ($CarProject -eq "factory")) {
    throw "Target 'full' is for car products only (boot+app+factory merge). Got CarProject=factory"
}

function Resolve-FlashImagePath {
    param([string]$FlashTarget)

    if ($FlashTarget -eq "full") {
        $hex = Join-Path $BuildDir "$CarProject-full.hex"
        $bin = Join-Path $BuildDir "$CarProject-full.bin"
        if (Test-Path $hex) { return $hex }
        if (Test-Path $bin) { return $bin }
        return $null
    }

    $presets = @{
        standalone = $CarProject
        bootloader = "bootloader"
        app        = "app"
        factory    = "factory"
    }
    $base = $presets[$FlashTarget]
    if (-not $base) { throw "Unknown target: $FlashTarget" }

    $elf = Join-Path $BuildDir "$base.elf"
    if (Test-Path $elf) { return $elf }
    $bin = Join-Path $BuildDir "$base.bin"
    if (Test-Path $bin) { return $bin }
    return $null
}

function Invoke-BuildForFlashTarget {
    param([string]$FlashTarget)

    $buildTarget = if ($FlashTarget -eq "full") { "all" } else { $FlashTarget }
    $logEn = if ($env:LOG_ENABLE) { $env:LOG_ENABLE } else { "1" }

    Write-Host "==> image missing for -Target $FlashTarget; building $CarProject -Target $buildTarget (LOG_ENABLE=$logEn)..." -ForegroundColor Yellow
    & $BuildPs1 -CarProject $CarProject -Target $buildTarget -Action build -LogEnable $logEn
    if ($LASTEXITCODE -ne 0) {
        throw "auto-build failed for $CarProject -Target $buildTarget (exit $LASTEXITCODE)"
    }
}

# Resolve image
if ([string]::IsNullOrWhiteSpace($Image)) {
    $Image = Resolve-FlashImagePath -FlashTarget $Target
    if (-not $Image) {
        Invoke-BuildForFlashTarget -FlashTarget $Target
        $Image = Resolve-FlashImagePath -FlashTarget $Target
    }
    if (-not $Image) {
        throw "No image found in $BuildDir for target $Target after auto-build"
    }
}
if (-not (Test-Path $Image)) { throw "Image not found: $Image" }

if (Test-JLinkSharedToWsl) { exit 1 }

if ($EraseAll -and $EraseApps) {
    throw "Use -EraseAll or -EraseApps, not both."
}

if ($Recover) {
    $EraseAll = $true
    if ($Speed -gt 100) {
        $Speed = 50
    }
}

Write-Host "Flashing $Image via JLink..." -ForegroundColor Cyan
Write-Host "  Car:    $CarProject" -ForegroundColor DarkGray
Write-Host "  Target: $Target" -ForegroundColor DarkGray
Write-Host "  Image:  $Image" -ForegroundColor DarkGray
Write-Host "  SWD:    ${Speed} kHz" -ForegroundColor DarkGray
if ($Recover) {
    Write-Host "  Mode:   RECOVER (erase chip + load; hold board RESET)" -ForegroundColor Yellow
}
if ($EraseAll) {
    Write-Host "  Erase:  full chip (256 KB)" -ForegroundColor Yellow
} elseif ($EraseApps) {
    Write-Host "  Erase:  0x4000..0x3FFFF (APP_A + APP_B + NVS)" -ForegroundColor Yellow
}

# Build JLink command file
$tmpDir = Join-Path $ProjectRoot "tmp"
New-Item -ItemType Directory -Force -Path $tmpDir | Out-Null
$cmdFile = Join-Path $tmpDir "flash.jlink"
$offsets = @{
    standalone = "0x0"
    bootloader = "0x0"
    app        = "0x4000"
    factory    = "0x21000"
    full       = "0x0"
}
# ELF/HEX carry VMA; BIN needs explicit offset
$useLoadFile = ($Image -like '*.elf') -or ($Image -like '*.hex')
$loadCmd = if ($useLoadFile) {
    "loadfile `"$Image`""
} else {
    $off = $offsets[$Target]
    if (-not $off) { $off = "0x0" }
    "loadbin `"$Image`", $off"
}

$eraseCmd = if ($EraseAll) {
    "erase"
} elseif ($EraseApps) {
    "erase 0x4000 0x40000"
} else {
    ""
}

# rsettype 2 = connect under reset (needs J-Link nRESET wired; else hold board RESET)
# r0 / r1 = assert / deassert probe RESET pin
$jlinkBody = @(
    "si SWD",
    "speed $Speed",
    "device TM4C123GH6PM",
    "rsettype 2",
    "r0",
    "sleep 200",
    "connect",
    "halt"
)
if ($eraseCmd) { $jlinkBody += $eraseCmd }
$jlinkBody += $loadCmd
$jlinkBody += @("r1", "r", "g", "exit")

($jlinkBody -join "`n") + "`n" | Set-Content -Path $cmdFile -Encoding ASCII

if (-not (Test-Path $JLinkExe)) {
    throw "J-Link not found: $JLinkExe"
}

$log = Join-Path $tmpDir "jlink-flash.log"

Write-Host ""
Write-Host "===== BEFORE FLASH: hold board RESET now =====" -ForegroundColor Yellow
Write-Host "  Keep holding until you see Erasing/Downloading (not only Connecting)." -ForegroundColor Yellow
Write-Host "  Unplug HC-SR04 Echo (PC1/SWDIO) if plugged." -ForegroundColor Yellow
Write-Host "  If J-Link nRESET is not wired to MCU, hand RESET is mandatory." -ForegroundColor Yellow
Write-Host "==============================================" -ForegroundColor Yellow
Write-Host ""
Start-Sleep -Seconds 2

$jlinkArgs = @("-AutoConnect", "1", "-CommanderScript", $cmdFile)
$p = Start-Process -NoNewWindow -Wait -PassThru -FilePath $JLinkExe -ArgumentList $jlinkArgs -RedirectStandardOutput $log
$logText = Get-Content $log -Raw
Get-Content $log

if ($logText -match 'Error occurred:' -or $logText -match 'Could not connect' -or
    $logText -match 'Failed to power up DAP' -or $logText -match 'Failed to halt CPU') {
    Write-Host ""
    Write-Host "J-Link connect/flash failed. See $log" -ForegroundColor Red
    Write-Host "Do this exactly:" -ForegroundColor Yellow
    Write-Host "  A. Power OFF board 5s, unplug ultrasonic Echo (PC1)" -ForegroundColor Yellow
    Write-Host "  B. Power ON, immediately HOLD board RESET (finger stays down)" -ForegroundColor Yellow
    Write-Host "  C. Run:" -ForegroundColor Yellow
    Write-Host "       .\flash-jlink.cmd -CarProject $CarProject -Recover" -ForegroundColor Cyan
    Write-Host "  D. Release RESET ONLY after log shows Erasing or Downloading" -ForegroundColor Yellow
    Write-Host ""
    Write-Host "Manual J-Link Commander (hold RESET the whole time):" -ForegroundColor DarkGray
    Write-Host "  si SWD" -ForegroundColor DarkGray
    Write-Host "  speed 50" -ForegroundColor DarkGray
    Write-Host "  device TM4C123GH6PM" -ForegroundColor DarkGray
    Write-Host "  rsettype 2" -ForegroundColor DarkGray
    Write-Host "  connect" -ForegroundColor DarkGray
    Write-Host "  halt" -ForegroundColor DarkGray
    Write-Host "  erase" -ForegroundColor DarkGray
    Write-Host "  loadfile `"$Image`"" -ForegroundColor DarkGray
    Write-Host "  r" -ForegroundColor DarkGray
    Write-Host "  g" -ForegroundColor DarkGray
    Write-Host "  exit" -ForegroundColor DarkGray
    exit 1
}

if ($p.ExitCode -ne 0) {
    Write-Host "J-Link failed (exit $($p.ExitCode)). See $log" -ForegroundColor Red
    exit $p.ExitCode
}

Write-Host "Done. See $log for details." -ForegroundColor Green
