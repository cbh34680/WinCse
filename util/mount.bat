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

setlocal
set drive=%~d0%
set drive=%drive:~0,1%


@echo on
if exist Y:\ net use Y: /delete
net use Y: "\\WinCse.aws-s3.Y\%drive%$%~p0..\..\MOUNT"

endlocal
