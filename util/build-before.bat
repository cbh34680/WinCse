echo.
echo ***** RUN-COMMAND: %~f0 *****
echo.

net session >nul 2>nul
if %errorlevel% neq 0 (
 powershell start-process %~0 -verb runas
 exit
)

echo on
pushd %~dp0..

if exist Y:\ net use Y: /delete
