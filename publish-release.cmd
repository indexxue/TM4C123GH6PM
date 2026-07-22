@echo off
setlocal
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\publish-release.ps1" %*
exit /b %ERRORLEVEL%
