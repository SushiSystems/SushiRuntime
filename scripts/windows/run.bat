@echo off
setlocal

pushd %~dp0

for /f "usebackq tokens=1,2 delims==" %%A in ("config.ini") do (
    if "%%A"=="ONEAPI_ROOT" set "ONEAPI_ROOT=%%B"
    if "%%A"=="VCPKG_ROOT" set "VCPKG_ROOT=%%B"
    if "%%A"=="TARGET_BIN" set "TARGET_BIN=%%B"
)

if exist "%ONEAPI_ROOT%\setvars.bat" (
    call "%ONEAPI_ROOT%\setvars.bat" intel64 >nul 2>&1
)

set "PATH=%~dp0..\..\build\src;%VCPKG_ROOT%\installed\x64-windows\bin;%PATH%"

if exist "..\..\build\examples\%TARGET_BIN%" (
    echo [INFO] Running %TARGET_BIN%...
    "..\..\build\examples\%TARGET_BIN%"
) else (
    echo [ERROR] Binary %TARGET_BIN% not found. Run scripts/windows/build.bat first.
)

popd
pause
