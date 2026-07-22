@echo off
setlocal
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0..\..\scripts\build.ps1" factory %*
exit /b %ERRORLEVEL%
