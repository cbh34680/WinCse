@echo off

@rem
@rem Get administrator rights
@rem
net session >nul 2>nul
if %errorlevel% neq 0 (
  cd "%~dp0"
  powershell.exe Start-Process -FilePath ".\%~nx0" -Verb runas
  exit
)

cd "%~dp0"
powershell.exe -ExecutionPolicy Bypass -Command ".\gui-compat-s3.ps1"

