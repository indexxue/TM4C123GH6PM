param()
$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent $PSScriptRoot
$GenScript   = Join-Path (Join-Path $ProjectRoot "scripts") "gen-board-config.py"
$SysCfgDir   = Join-Path $ProjectRoot ".syscfg"

if (-not (Test-Path $GenScript)) {
    throw "Not found: $GenScript"
}
if (-not (Test-Path (Join-Path $SysCfgDir "project.json"))) {
    Write-Host ".syscfg/project.json not found, skip code generation" -ForegroundColor Yellow
    exit 0
}

Write-Host "=== SysConfig: generating board config ===" -ForegroundColor Cyan
python "$GenScript" --cfg "$SysCfgDir\project.json"
if ($LASTEXITCODE -ne 0) { throw "SysConfig generation failed" }