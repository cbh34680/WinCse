@echo off
cd "%~dp0"

if not exist my-debug-test.bat (
  @echo my-debug-test.bat: no such file
  @pause
  exit
)

@call my-debug-test.bat
@pause
