param(
    [Parameter(Mandatory = $true)][string]$Image,
    [string]$Offset = "0x00000000"
)
$ErrorActionPreference = "Stop"
$Dslite = "D:\Ti\uniflash_9.2.0\dslite.bat"
if (-not (Test-Path $Dslite)) { throw "UniFlash dslite.bat not found at $Dslite" }
if (-not (Test-Path $Image)) { throw "Image not found: $Image" }

Write-Host "Flashing TM4C123GH6PM..." -ForegroundColor Cyan
Write-Host "  Image:  $Image"
Write-Host "  Offset: $Offset"
Write-Host "  Tool:   $Dslite"

& $Dslite --mode flash --config "TM4C123GH6PM.ccxml" --flash $Image --offset $Offset
if ($LASTEXITCODE -ne 0) { throw "UniFlash failed (exit $LASTEXITCODE)" }
