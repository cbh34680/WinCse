@echo off

net session >nul 2>nul
if %errorlevel% neq 0 (
 @powershell start-process %~0 -verb runas
 exit
)

@echo on
echo RUN-COMMAND: %~f0
call "%ProgramFiles(x86)%\WinFsp\bin\fsreg.bat" WinCse.aws-s3.Y %~dp0..\x64\Debug\WinCse.exe "-u %%%%1 -m %%%%2 -d -1 -D %~dp0..\trace\winfsp.log -T %~dp0..\trace" "D:P(A;;RPWPLC;;;WD)"
pause
