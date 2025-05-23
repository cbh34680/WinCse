@rem
@rem https://github.com/googleapis/google-cloud-cpp/blob/main/google/cloud/storage/quickstart/README.md
@rem https://github.com/googleapis/google-cloud-cpp/blob/main/google/cloud/storage/CMakeLists.txt
@rem

chcp 65001

set VCPKG_ROOT=%~dp0repo
set VCPKG_BINARY_SOURCES=clear
set VCPKG_DEFAULT_TRIPLET=x64-windows-cxx17

set PATH=%VCPKG_ROOT%;%PATH%
vcpkg --version
vcpkg list

vcpkg install

vcpkg remove --outdated
vcpkg list

if not exist dest\Debug (
  mkdir dest\Debug
)

if not exist dest\Release (
  mkdir dest\Release
)

xcopy /I /E /Y %VCPKG_ROOT%\installed\%VCPKG_DEFAULT_TRIPLET%\include   dest\Debug\include
xcopy /I /E /Y %VCPKG_ROOT%\installed\%VCPKG_DEFAULT_TRIPLET%\debug\bin dest\Debug\bin
xcopy /I /E /Y %VCPKG_ROOT%\installed\%VCPKG_DEFAULT_TRIPLET%\debug\lib dest\Debug\lib

xcopy /I /E /Y %VCPKG_ROOT%\installed\%VCPKG_DEFAULT_TRIPLET%\include   dest\Release\include
xcopy /I /E /Y %VCPKG_ROOT%\installed\%VCPKG_DEFAULT_TRIPLET%\bin       dest\Release\bin
xcopy /I /E /Y %VCPKG_ROOT%\installed\%VCPKG_DEFAULT_TRIPLET%\lib       dest\Release\lib

