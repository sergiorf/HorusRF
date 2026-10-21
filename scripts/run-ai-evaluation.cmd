@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0run-ai-evaluation.ps1" %*
exit /b %errorlevel%
