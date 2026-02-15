@echo off
setlocal enabledelayedexpansion

pushd %~dp0

if not exist "config.ini" (
    echo [ERROR] config.ini not found in scripts/windows folder!
    popd
    pause
    exit /b 1
)

for /f "usebackq tokens=1,2 delims==" %%A in ("config.ini") do (
    set "key=%%A"
    set "value=%%B"
    for /f "tokens=*" %%X in ("!key!") do set "key=%%X"
    for /f "tokens=*" %%X in ("!value!") do set "value=%%X"
    
    if "!key!"=="ONEAPI_ROOT" set "CFG_ONEAPI=!value!"
    if "!key!"=="VCPKG_ROOT" set "CFG_VCPKG=!value!"
    if "!key!"=="VS_ROOT" set "CFG_VS=!value!"
    if "!key!"=="NINJA_EXE" set "CFG_NINJA=!value!"
    if "!key!"=="VCVARS_BAT" set "CFG_VCVARS=!value!"
)

if exist "%CFG_VCVARS%" (
    echo [INFO] Loading VS vars...
    call "%CFG_VCVARS%" >nul
)

if exist "%CFG_ONEAPI%/setvars.bat" (
    echo [INFO] Loading oneAPI vars...
    call "%CFG_ONEAPI%/setvars.bat" intel64 >nul 2>&1
)

set "VCPKG_ROOT=%CFG_VCPKG%"
set "PATH=%CFG_VCPKG%\installed\x64-windows\tools\pkgconf;%CFG_VCPKG%;%PATH%"
set "CMAKE_TOOLCHAIN_FILE=%CFG_VCPKG%/scripts/buildsystems/vcpkg.cmake"

cd ..\..

if not exist "build\build.ninja" (
    echo [INFO] Configuring CMake...
    if exist build rmdir /s /q build
    
    cmake -B build -G Ninja ^
        -DCMAKE_BUILD_TYPE=Release ^
        -DCMAKE_TOOLCHAIN_FILE="%CMAKE_TOOLCHAIN_FILE%" ^
        -DVCPKG_ROOT="%VCPKG_ROOT%" ^
        -DVCPKG_TARGET_TRIPLET=x64-windows ^
        -DCMAKE_MAKE_PROGRAM="%CFG_NINJA%" ^
        -DCMAKE_CXX_COMPILER="%CFG_ONEAPI%/compiler/latest/bin/icx-cl.exe" ^
        -DCMAKE_C_COMPILER="%CFG_ONEAPI%/compiler/latest/bin/icx-cl.exe" ^
        -DCMAKE_CXX_FLAGS="-fsycl" ^
        -DPKG_CONFIG_EXECUTABLE="%CFG_VCPKG%/installed/x64-windows/tools/pkgconf/pkgconf.exe" ^
        -DCMAKE_PREFIX_PATH="%CFG_VCPKG%/installed/x64-windows"
)

echo [INFO] Building...
cmake --build build --config Release

if %ERRORLEVEL% EQU 0 (
    echo [SUCCESS] Build completed successfully!
) else (
    echo [ERROR] Build failed.
)

popd
pause
