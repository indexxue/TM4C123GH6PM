# Shared TivaWare SDK paths (local sdk/, not committed).
$ErrorActionPreference = "Stop"

$Script:ProjectRoot = Split-Path -Parent $PSScriptRoot
$Script:TivaWareVersion = "2.2.0.295"
$Script:SdkDir = Join-Path $ProjectRoot "sdk"
$Script:TivaWareRoot = Join-Path $SdkDir "TivaWare_C_Series-$TivaWareVersion"
$Script:TivaWareLib = Join-Path $TivaWareRoot "driverlib\gcc\libdriver.a"
$Script:FreeRTOSRoot = Join-Path $TivaWareRoot "third_party\FreeRTOS\Source"
$Script:LegacyTivaWareRoot = "D:\Ti\TivaWare_C_Series-$TivaWareVersion"

function Test-TivaWareInstalled {
    param([string]$Root = $Script:TivaWareRoot)

    return (Test-Path (Join-Path $Root "driverlib\gcc\libdriver.a")) -and
           (Test-Path (Join-Path $Root "third_party\FreeRTOS\Source\tasks.c"))
}

function Ensure-TivaWare {
    if (Test-TivaWareInstalled) {
        return
    }

    Write-Host "TivaWare SDK not found. Installing to $Script:TivaWareRoot ..." -ForegroundColor Yellow
    & (Join-Path $PSScriptRoot "install-tivaware.ps1")
    if (-not (Test-TivaWareInstalled)) {
        throw "TivaWare SDK not found at $Script:TivaWareRoot (see docs/build.md)."
    }
}
