@rem
@rem https://github.com/googleapis/google-cloud-cpp/blob/main/google/cloud/storage/quickstart/README.md
@rem https://github.com/googleapis/google-cloud-cpp/blob/main/google/cloud/storage/CMakeLists.txt
@rem

chcp 65001

set VCPKG_ROOT=%~dp0repo
set VCPKG_BINARY_SOURCES=clear
set VCPKG_DEFAULT_TRIPLET=x64-windows-cxx17

rd /Q /S %VCPKG_ROOT%
rd /Q /S dest

mkdir %VCPKG_ROOT%

pushd %VCPKG_ROOT%
git clone https://github.com/microsoft/vcpkg.git .
call .\bootstrap-vcpkg.bat
popd

copy ..\doc\vcpkg\triplets\%VCPKG_DEFAULT_TRIPLET%.cmake %VCPKG_ROOT%\triplets\
copy ..\doc\vcpkg\vcpkg.json .\

set PATH=%VCPKG_ROOT%;%PATH%
vcpkg --version
vcpkg list

vcpkg install
