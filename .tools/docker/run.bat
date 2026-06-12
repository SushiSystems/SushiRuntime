@echo off
echo [INFO] Starting SushiRuntime developer container with volume...

set ADMIN_FLAGS=
if "%~1"=="--admin" set ADMIN_FLAGS=--privileged --cap-add=SYS_ADMIN
if "%~2"=="--admin" set ADMIN_FLAGS=--privileged --cap-add=SYS_ADMIN

docker run -it --rm ^
    %ADMIN_FLAGS% ^
    -v "%CD%:/workspace/sushiruntime" ^
    sushiruntime /bin/bash