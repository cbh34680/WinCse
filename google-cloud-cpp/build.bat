@rem
@rem https://github.com/googleapis/google-cloud-cpp/blob/main/google/cloud/storage/quickstart/README.md
@rem https://github.com/googleapis/google-cloud-cpp/blob/main/google/cloud/storage/CMakeLists.txt
@rem

chcp 65001

set VCPKG_ROOT=%~dp0repo
set VCPKG_BINARY_SOURCES=clear
set VCPKG_DEFAULT_TRIPLET=x64-windows-cxx17

if exist %VCPKG_ROOT% (
  pushd %VCPKG_ROOT%
  git pull
  call .\bootstrap-vcpkg.bat
  popd
) else (
  mkdir %VCPKG_ROOT%

  pushd %VCPKG_ROOT%
  git clone https://github.com/microsoft/vcpkg.git .
  call .\bootstrap-vcpkg.bat
  popd
)

copy ..\doc\vcpkg\triplets\%VCPKG_DEFAULT_TRIPLET%.cmake %VCPKG_ROOT%\triplets\

set PATH=%VCPKG_ROOT%;%PATH%
vcpkg --version
vcpkg list

if exist %VCPKG_ROOT%\installed\%VCPKG_DEFAULT_TRIPLET%\lib\google_cloud_cpp_storage.lib (
  vcpkg upgrade --no-dry-run
  vcpkg install google-cloud-cpp[core,storage]
) else (
  vcpkg install google-cloud-cpp[core,storage]
)

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

