@echo off

net session >nul 2>nul
if %errorlevel% neq 0 (
 @powershell start-process %~0 -verb runas
 exit
)

@echo on
echo RUN-COMMAND: %~f0

call %~dp0setvars.bat

call "%ProgramFiles(x86)%\WinFsp\bin\fsreg.bat" -u %REGKEY%

pause
