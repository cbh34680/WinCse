@echo off

net session >nul 2>nul
if %errorlevel% neq 0 (
 powershell start-process %~0 -verb runas
 exit
)


@cd "%~dp0"

@call handle.exe -p WinCse.exe -s
@pause
