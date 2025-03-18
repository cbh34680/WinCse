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

for /f "tokens=2 delims=," %%A in ('tasklist /fi "imagename eq WinCse.exe" /fo csv /nh') do (
  set "pid=%%A"
)

if not defined pid (
  exit
)

call taskkill /IM WinCse.exe /F

