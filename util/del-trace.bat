echo RUN-COMMAND: %~f0

pushd "%~dp0.."
del /Q trace\*.log
del /Q trace\*.txt
popd
