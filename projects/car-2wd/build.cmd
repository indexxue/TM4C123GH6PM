@echo off
setlocal
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0..\..\scripts\build.ps1" -CarProject car-2wd %*
exit /b %ERRORLEVEL%
