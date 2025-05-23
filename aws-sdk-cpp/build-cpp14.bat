@rem
@rem CMake parameters
@rem https://docs.aws.amazon.com/sdk-for-cpp/v1/developer-guide/cmake-params.html
@rem

git --version
cmake --version
if exist repo (
  pushd repo
  git pull
  popd repo
) else (
  git clone --recurse-submodules https://github.com/aws/aws-sdk-cpp repo
)
mkdir build
pushd build
cmake ..\repo -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=..\dest\Release -DBUILD_ONLY="s3" -DCPP_STANDARD=14
cmake --build . --config=Release
cmake --install . --config=Release
popd
rd /Q /S build
mkdir build
pushd build
cmake ..\repo -DCMAKE_BUILD_TYPE=Debug -DCMAKE_INSTALL_PREFIX=..\dest\Debug -DBUILD_ONLY="s3" -DCPP_STANDARD=14
cmake --build . --config=Debug
cmake --install . --config=Debug
popd
rd /Q /S build
