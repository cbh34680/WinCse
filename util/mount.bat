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

set DRIVELETTER=%~d0%
set DRIVELETTER=%DRIVELETTER:~0,1%

@echo on
if exist Y:\ net use Y: /delete

call %~dp0setvars.bat

net use Y: "\\%REGKEY%\%DRIVELETTER%$%~p0..\..\MOUNT"

