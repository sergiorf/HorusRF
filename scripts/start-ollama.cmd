@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0start-ollama.ps1" %*
exit /b %errorlevel%
