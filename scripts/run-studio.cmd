@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0run-studio.ps1" %*
exit /b %errorlevel%
