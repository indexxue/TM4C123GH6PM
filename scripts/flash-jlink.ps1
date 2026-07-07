param(
    [string]$Image = "",
    [ValidateSet("standalone", "bootloader", "app", "factory")]
    [string]$Target = "standalone",
    [ValidateSet("car-4wd", "car-2wd")]
    [string]$CarProject = "car-4wd"
)
$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent $PSScriptRoot
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

Write-Host "Flashing $Image via JLink..." -ForegroundColor Cyan
Write-Host "  Car:    $CarProject" -ForegroundColor DarkGray
Write-Host "  Target: $Target" -ForegroundColor DarkGray
Write-Host "  Image:  $Image" -ForegroundColor DarkGray

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

@"
si SWD
speed 1000
device TM4C123GH6PM
connect
r
halt
$loadCmd
r
g
exit
"@ | Set-Content -Path $cmdFile -Encoding ASCII

# Run JLink
$log = Join-Path $tmpDir "jlink-flash.log"
Start-Process -NoNewWindow -Wait -FilePath $JLinkExe -ArgumentList $cmdFile -RedirectStandardOutput $log
Get-Content $log

Write-Host "Done. See $log for details." -ForegroundColor Green
