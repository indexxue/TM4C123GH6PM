@echo off
setlocal
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\flash-jlink.ps1" %*
exit /b %ERRORLEVEL%
