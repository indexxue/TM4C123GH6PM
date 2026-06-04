param()
$ErrorActionPreference="Stop"
$ProjectRoot=Split-Path -Parent $PSScriptRoot
$SysCfgTool="D:\Ti\sysconfig_1.27.1\sysconfig_cli.bat"
$SysCfgFile=Join-Path $ProjectRoot ".syscfg\tm4c123gh6pm.syscfg"
$TivaWareRoot="D:\Ti\TivaWare_C_Series-2.2.0.295"
$ProductJson=Join-Path $TivaWareRoot "docs\sw\sysconfig\products.json"
$OutputDir=Join-Path $ProjectRoot "src\generated"
$FallbackPy=Join-Path $ProjectRoot "scripts\gen-board-config.py"
$UseSysConfig=(Test-Path $SysCfgTool)-and(Test-Path $ProductJson)-and(Test-Path $SysCfgFile)
if(-not $UseSysConfig){
    if(Test-Path $FallbackPy){
        Write-Host "=== SysConfig: using Python fallback ===" -ForegroundColor Yellow
        & python "$FallbackPy"
        exit
    }
    Write-Host "[SKIP] No SysConfig" -ForegroundColor Yellow
    exit 0
}
Write-Host "=== SysConfig CLI: generating board config ===" -ForegroundColor Cyan
New-Item -ItemType Directory -Force -Path $OutputDir|Out-Null
& $SysCfgTool --device TM4C123GH6PM --product "$ProductJson" --script "$SysCfgFile" --output "$OutputDir" --compiler gcc --quiet
if($LASTEXITCODE-ne0){
    Write-Host "WARNING: SysConfig CLI failed, falling back" -ForegroundColor Yellow
    & python "$FallbackPy"
    exit
}
Write-Host "SysConfig CLI done." -ForegroundColor Green
