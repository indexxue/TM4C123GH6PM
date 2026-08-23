@echo off
setlocal
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0..\..\scripts\build.ps1" rc-controller %*
exit /b %ERRORLEVEL%
