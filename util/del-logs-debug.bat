echo RUN-COMMAND: %~f0

pushd %~dp0..
del /Q trace\trace-*.*
popd
