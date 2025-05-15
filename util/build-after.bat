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

cd

del /Q trace\*.log
del /Q trace\*.txt

if not "%1"=="" xcopy /EXCLUDE:util\xcopy-excl-aws-sdk-cpp-dll.txt      /D /Y /I aws-sdk-cpp\dest\%1\bin\*.dll x64\%1
if not "%1"=="" xcopy /EXCLUDE:util\xcopy-excl-google-cloud-cpp-dll.txt /D /Y /I google-cloud-cpp\dest\%1\bin\*.dll x64\%1

call %~dp0setvars.bat

call "%ProgramFiles(x86)%\WinFsp\bin\fsreg.bat" %REGKEY% %~dp0..\x64\%1\WinCse.exe "-u %%%%1 -m %%%%2 -d -1 -D %~dp0..\trace\winfsp.log -T %~dp0..\trace" "D:P(A;;RPWPLC;;;WD)"
reg query HKLM\Software\WinFsp\Services\%REGKEY% /s /reg:32

popd
