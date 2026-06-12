param()
$ErrorActionPreference="Stop"
$ProjectRoot=Split-Path -Parent $PSScriptRoot
$SysCfgTool="D:\Ti\sysconfig_1.27.1\sysconfig_cli.bat"
$SysCfgFile=Join-Path $ProjectRoot ".syscfg\tm4c123gh6pm.syscfg"
$OutputDir=Join-Path $ProjectRoot "Common\src"
$FallbackPy=Join-Path $ProjectRoot "scripts\gen-board-config.py"

# Try using SysConfig CLI (product.json auto-detected from device data)
if((Test-Path $SysCfgTool)-and(Test-Path $SysCfgFile)){
    Write-Host "=== SysConfig CLI: generating board config ===" -ForegroundColor Cyan
    New-Item -ItemType Directory -Force -Path $OutputDir|Out-Null
    & $SysCfgTool --device TM4C123GH6PM --script "$SysCfgFile" --output "$OutputDir" --compiler gcc --quiet
    if($LASTEXITCODE-eq0){
        Write-Host "SysConfig CLI done." -ForegroundColor Green
        exit
    }
    Write-Host "WARNING: SysConfig CLI failed, falling back to Python" -ForegroundColor Yellow
}
# Fallback: use Python generator
if(Test-Path $FallbackPy){
    Write-Host "=== SysConfig: using Python fallback ===" -ForegroundColor Yellow
    & python "$FallbackPy"
    exit
}
Write-Host "[SKIP] No SysConfig or fallback" -ForegroundColor Yellow
