@echo off

net session >nul 2>nul
if %errorlevel% neq 0 (
 @powershell start-process %~0 -verb runas
 exit
)

@cd "%~dp0"

for /f "tokens=2 delims=," %%A in ('tasklist /fi "imagename eq WinCse.exe" /fo csv /nh') do (
  set "pid=%%A"
)

if not defined pid (
  @echo no such process
  @pause
  exit
)

@echo pid is %pid%
start x64\Debug\util-SendCtrlBreak.exe %pid%

