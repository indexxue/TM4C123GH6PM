# One-time terminal setup: allow local .ps1 scripts and unblock project scripts.
$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent $PSScriptRoot

Write-Host "Setting ExecutionPolicy (CurrentUser) -> RemoteSigned ..." -ForegroundColor Cyan
try {
    Set-ExecutionPolicy -Scope CurrentUser -ExecutionPolicy RemoteSigned -Force
    Write-Host "  OK: $(Get-ExecutionPolicy -Scope CurrentUser)" -ForegroundColor Green
} catch {
    Write-Host "  Skipped (group policy or admin lock): $($_.Exception.Message)" -ForegroundColor Yellow
    Write-Host "  Use .\build.cmd instead — it does not require changing execution policy." -ForegroundColor Yellow
}

Get-ChildItem -Path (Join-Path $ProjectRoot "scripts") -Filter "*.ps1" -File |
    ForEach-Object {
        Unblock-File -LiteralPath $_.FullName -ErrorAction SilentlyContinue
    }
Unblock-File -LiteralPath (Join-Path $ProjectRoot "build.cmd") -ErrorAction SilentlyContinue

Write-Host "Done. You can build with:" -ForegroundColor Green
Write-Host "  .\scripts\build.ps1"
Write-Host "  .\build.cmd"
