call %~dp0setvars.bat

reg query HKLM\Software\WinFsp\Services\%REGKEY% /s /reg:32

pause
