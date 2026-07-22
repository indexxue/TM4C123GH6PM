param(
    [string]$Image = "",
    [ValidateSet("standalone", "bootloader", "app", "factory")]
    [string]$Target = "standalone",
    [ValidateSet("factory", "car-4wd", "car-2wd")]
    [string]$CarProject = "car-4wd",
    [switch]$EraseAll,
    [switch]$EraseApps,
    [int]$Speed = 400
)
$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent $PSScriptRoot

# factory image always lives under projects/factory/build/
if ($Target -eq "factory") {
    $CarProject = "factory"
}

$BuildDir = Join-Path $ProjectRoot "projects\$CarProject\build"
$JLinkExe = "C:\Program Files\SEGGER\JLink_V818\JLink.exe"

# Resolve image
if ([string]::IsNullOrWhiteSpace($Image)) {
    $presets = @{
        standalone = $CarProject
        bootloader = "bootloader"
        app        = "app"
        factory    = "factory"
    }
    $base = $presets[$Target]
    if (-not $base) { throw "Unknown target: $Target. Valid: standalone, bootloader, app, factory" }

    $elf = Join-Path $BuildDir "$base.elf"
    if (Test-Path $elf) { $Image = $elf }
    else {
        $bin = Join-Path $BuildDir "$base.bin"
        if (Test-Path $bin) { $Image = $bin }
        else { throw "No image found in $BuildDir for target $Target" }
    }
}
if (-not (Test-Path $Image)) { throw "Image not found: $Image" }

if ($EraseAll -and $EraseApps) {
    throw "Use -EraseAll or -EraseApps, not both."
}

Write-Host "Flashing $Image via JLink..." -ForegroundColor Cyan
Write-Host "  Car:    $CarProject" -ForegroundColor DarkGray
Write-Host "  Target: $Target" -ForegroundColor DarkGray
Write-Host "  Image:  $Image" -ForegroundColor DarkGray
Write-Host "  SWD:    ${Speed} kHz" -ForegroundColor DarkGray
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
}
$isElf = $Image -like '*.elf'
$loadCmd = if ($isElf) {
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

# halt 后再 load；若仍连不上，按住 RESET 到出现 Connecting...
$jlinkBody = @(
    "si SWD",
    "speed $Speed",
    "device TM4C123GH6PM",
    "connect",
    "halt"
)
if ($eraseCmd) { $jlinkBody += $eraseCmd }
$jlinkBody += $loadCmd
$jlinkBody += @("r", "g", "exit")

($jlinkBody -join "`n") + "`n" | Set-Content -Path $cmdFile -Encoding ASCII

if (-not (Test-Path $JLinkExe)) {
    throw "J-Link not found: $JLinkExe"
}

$log = Join-Path $tmpDir "jlink-flash.log"
Write-Host "  Tip: if connect fails, hold RESET during 'Connecting to target via SWD'" -ForegroundColor DarkGray

$jlinkArgs = @("-AutoConnect", "1", "-CommanderScript", $cmdFile)
$p = Start-Process -NoNewWindow -Wait -PassThru -FilePath $JLinkExe -ArgumentList $jlinkArgs -RedirectStandardOutput $log
$logText = Get-Content $log -Raw
Get-Content $log

if ($logText -match 'Error occurred:' -or $logText -match 'Could not connect') {
    Write-Host "J-Link connect/flash failed. See $log" -ForegroundColor Red
    Write-Host "  1. Use -Target standalone for full car firmware (not app.elf @ 0x4000 only)" -ForegroundColor Yellow
    Write-Host "  2. Unplug HC-SR04 Echo (PC1) if wired" -ForegroundColor Yellow
    Write-Host "  3. Hold RESET, rerun flash, release when connecting" -ForegroundColor Yellow
    exit 1
}

if ($p.ExitCode -ne 0) {
    Write-Host "J-Link failed (exit $($p.ExitCode)). See $log" -ForegroundColor Red
    exit $p.ExitCode
}

Write-Host "Done. See $log for details." -ForegroundColor Green
