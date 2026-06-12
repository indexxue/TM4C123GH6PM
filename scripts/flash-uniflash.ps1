param(
    [ValidateSet("standalone", "bootloader", "app", "factory", "prod", "all")]
    [string]$Target = "standalone",
    [string]$Image = "",
    [string]$Offset = "",
    [string]$Project = "tm4c123-project"
)
$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent $PSScriptRoot
$BuildDir = Join-Path $ProjectRoot "build"
$Dslite = "D:\Ti\uniflash_9.2.0\dslite.bat"

if (-not (Test-Path $Dslite)) { throw "UniFlash dslite.bat not found at $Dslite" }

$SlotPresets = [ordered]@{
    standalone = @{ Bin = "$Project.bin";     Offset = "0x00000000" }
    bootloader = @{ Bin = "bootloader.bin";   Offset = "0x00000000" }
    app        = @{ Bin = "app.bin";          Offset = "0x00004000" }
    factory    = @{ Bin = "factory.bin";     Offset = "0x00021000" }
}

$TargetPresets = [ordered]@{
    prod = @("bootloader", "app")
    all  = @("bootloader", "app", "factory")
}

function Invoke-FlashSlot {
    param([string]$Slot)

    $preset = $SlotPresets[$Slot]
    $binName = $preset.Bin -replace '\$Project', $Project
    $imagePath = Join-Path $BuildDir $binName
    $offset = $preset.Offset

    if (-not (Test-Path $imagePath)) {
        throw "Image not found: $imagePath (run build.ps1 -Target $Slot first)"
    }

    Write-Host ">>> $binName @ $offset" -ForegroundColor Cyan
    & $Dslite --mode flash --config "TM4C123GH6PM.ccxml" --flash $imagePath --offset $offset
    if ($LASTEXITCODE -ne 0) { throw "UniFlash failed: $binName (exit $LASTEXITCODE)" }
}

function Invoke-FlashManual {
    param([string]$ImagePath, [string]$ImageOffset)

    if (-not (Test-Path $ImagePath)) { throw "Image not found: $ImagePath" }
    if ([string]::IsNullOrWhiteSpace($ImageOffset)) { $ImageOffset = "0x00000000" }

    Write-Host ">>> $ImagePath @ $ImageOffset" -ForegroundColor Cyan
    & $Dslite --mode flash --config "TM4C123GH6PM.ccxml" --flash $ImagePath --offset $ImageOffset
    if ($LASTEXITCODE -ne 0) { throw "UniFlash failed (exit $LASTEXITCODE)" }
}

Write-Host "Flashing TM4C123GH6PM" -ForegroundColor Green

if (-not [string]::IsNullOrWhiteSpace($Image)) {
    Invoke-FlashManual -ImagePath $Image -ImageOffset $Offset
    Write-Host "Done." -ForegroundColor Green
    return
}

if ($TargetPresets.Contains($Target)) {
    foreach ($slot in $TargetPresets[$Target]) {
        Invoke-FlashSlot -Slot $slot
    }
    Write-Host "Done ($Target)." -ForegroundColor Green
    return
}

if ($Target -eq "app") {
    Write-Host "Note: app @ APP_A requires bootloader already @ 0x0" -ForegroundColor DarkYellow
}

Invoke-FlashSlot -Slot $Target
Write-Host "Done." -ForegroundColor Green
