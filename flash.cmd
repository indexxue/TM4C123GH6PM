@echo off
setlocal
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\flash-uniflash.ps1" %*
exit /b %ERRORLEVEL%
