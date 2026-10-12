@echo off
REM Launcher for setup-ai-windows11.ps1 (double-click or cmd).
REM Passes all args through. Example:
REM   setup-ai-windows11.bat -RepoRoot D:\src\SamNPlayer -Yes
REM   setup-ai-windows11.bat -CheckOnly

setlocal
cd /d "%~dp0"

where powershell >nul 2>&1
if errorlevel 1 (
  echo PowerShell not found. Open Windows Terminal and run:
  echo   powershell -ExecutionPolicy Bypass -File "%~dp0setup-ai-windows11.ps1" %*
  exit /b 1
)

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0setup-ai-windows11.ps1" %*
exit /b %ERRORLEVEL%
