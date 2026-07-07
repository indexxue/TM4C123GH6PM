param(
    [ValidateSet("standalone", "bootloader", "app", "factory", "prod", "all")]
    [string]$Target = "standalone",
    [string]$Image = "",
    [string]$Project = "tm4c123-project",
    [string]$Ccxml = "",
    [switch]$Bin
)
$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent $PSScriptRoot
$BuildDir = Join-Path $ProjectRoot "build"
$Dslite = "D:\Ti\uniflash_9.2.0\dslite.bat"

if (-not (Test-Path $Dslite)) { throw "UniFlash dslite.bat not found at $Dslite" }

if ([string]::IsNullOrWhiteSpace($Ccxml)) {
    $Ccxml = Join-Path $ProjectRoot "TM4C123GH6PM.ccxml"
}
if (-not (Test-Path $Ccxml)) {
    throw @"
ccxml not found: $Ccxml
In UniFlash GUI: XDS110 + TM4C123GH6PM -> [download ccxml] -> save to project root.
"@
}

$SlotPresets = [ordered]@{
    standalone = @{ Elf = "$Project.elf"; Bin = "$Project.bin" }
    bootloader = @{ Elf = "bootloader.elf"; Bin = "bootloader.bin" }
    app        = @{ Elf = "app.elf"; Bin = "app.bin" }
    factory    = @{ Elf = "factory.elf"; Bin = "factory.bin" }
}

$TargetPresets = [ordered]@{
    prod = @("bootloader", "app")
    all  = @("bootloader", "app", "factory")
}

function Resolve-FlashImage {
    param([string]$Slot)

    $preset = $SlotPresets[$Slot]
    $elfName = $preset.Elf -replace '\$Project', $Project
    $binName = $preset.Bin -replace '\$Project', $Project
    $elfPath = Join-Path $BuildDir $elfName
    $binPath = Join-Path $BuildDir $binName

    if ($Bin) {
        if (-not (Test-Path $binPath)) {
            throw "Image not found: $binPath (run build.ps1 -Target $Slot first)"
        }
        return $binPath
    }

    if (Test-Path $elfPath) { return $elfPath }
    if (Test-Path $binPath) {
        Write-Host "Note: using $binName (prefer .elf for correct flash address)" -ForegroundColor DarkYellow
        return $binPath
    }
    throw "Image not found: $elfPath (run build.ps1 -Target $Slot first)"
}

function Invoke-DsliteFlash {
    param([string[]]$Images)

    foreach ($imagePath in $Images) {
        $name = Split-Path $imagePath -Leaf
        Write-Host ">>> $name" -ForegroundColor Cyan
    }

    # UniFlash 9.x: no --offset; .elf link address selects flash offset automatically.
    # dslite.bat already defaults to flash mode; do not pass a leading "flash" arg.
    & $Dslite --config $Ccxml --flash --verify --verbose --reset 0 @Images
    if ($LASTEXITCODE -ne 0) {
        throw @"
UniFlash failed (exit $LASTEXITCODE).
If you see Error -1170: SWD wiring (TCK->PC0, TMS->PC1, GND, NRST), disconnect PC0/PC1 peripherals, lower adapter speed in ccxml, then retry.
"@
    }
}

Write-Host "Flashing TM4C123GH6PM via UniFlash DSLite" -ForegroundColor Green
Write-Host "  ccxml: $Ccxml" -ForegroundColor DarkGray
Write-Host "  tip: default uses .elf (link address = flash offset); -Bin forces .bin @ 0x0" -ForegroundColor DarkGray

if (-not [string]::IsNullOrWhiteSpace($Image)) {
    if (-not (Test-Path $Image)) { throw "Image not found: $Image" }
    Invoke-DsliteFlash -Images @((Resolve-Path $Image).Path)
    Write-Host "Done." -ForegroundColor Green
    return
}

if ($TargetPresets.Contains($Target)) {
    $images = @()
    foreach ($slot in $TargetPresets[$Target]) {
        $images += (Resolve-FlashImage -Slot $slot)
    }
    Invoke-DsliteFlash -Images $images
    Write-Host "Done ($Target)." -ForegroundColor Green
    return
}

if ($Target -eq "app") {
    Write-Host "Note: app @ APP_A requires bootloader already @ 0x0" -ForegroundColor DarkYellow
}

Invoke-DsliteFlash -Images @(Resolve-FlashImage -Slot $Target)
Write-Host "Done." -ForegroundColor Green
