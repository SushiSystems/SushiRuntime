@echo off
setlocal enabledelayedexpansion

pushd %~dp0

if not exist "..\config.ini" (
    echo [ERROR] config.ini not found in .tools/ folder!
    echo [HINT] Please copy config.ini.template to config.ini in .tools/ folder and edit it with your paths.
    popd
    pause
    exit /b 1
)

for /f "usebackq tokens=1,2 delims==" %%A in ("..\config.ini") do (
    set "key=%%A"
    set "value=%%B"
    for /f "tokens=*" %%X in ("!key!") do set "key=%%X"
    for /f "tokens=*" %%X in ("!value!") do set "value=%%X"
    
    if "!key!"=="ONEAPI_ROOT" set "CFG_ONEAPI=!value!"
    if "!key!"=="VCPKG_ROOT" set "CFG_VCPKG=!value!"
    if "!key!"=="VS_ROOT" set "CFG_VS=!value!"
    if "!key!"=="NINJA_EXE" set "CFG_NINJA=!value!"
    if "!key!"=="VCVARS_BAT" set "CFG_VCVARS=!value!"
    if "!key!"=="ICX_COMPILER" set "CFG_ICX=!value!"
    if "!key!"=="PKGCONF_EXE" set "CFG_PKGCONF=!value!"
    if "!key!"=="VCPKG_TRIPLET" set "CFG_TRIPLET=!value!"
)

if exist "%CFG_VCVARS%" (
    echo [INFO] Loading VS vars...
    call "%CFG_VCVARS%" >nul
)

if exist "%CFG_ONEAPI%\setvars.bat" (
    echo [INFO] Loading oneAPI vars...
    call "%CFG_ONEAPI%\setvars.bat" intel64 >nul 2>&1
)

set "VCPKG_ROOT=%CFG_VCPKG%"
set "PATH=%CFG_VCPKG%\installed\%CFG_TRIPLET%\tools\pkgconf;%CFG_VCPKG%;%PATH%"
set "LIB=%CFG_VCPKG%\..\..\lib;%LIB%"
set "INCLUDE=%CFG_VCPKG%\..\..\include;%CFG_VCPKG%\..\..\..\include;%INCLUDE%"
set "CMAKE_TOOLCHAIN_FILE=%CFG_VCPKG%\scripts\buildsystems\vcpkg.cmake"

cd ..\..

REM Distributed layer toggle (default OFF), passed in via the SUSHI_ENABLE_DISTRIBUTED
REM env var by the Python CLI's --distributed flag. It alters Graph::Node's layout
REM (PUBLIC compile def), so flipping it forces a fresh configure via a stamp file.
if "%SUSHI_ENABLE_DISTRIBUTED%"=="" set "SUSHI_ENABLE_DISTRIBUTED=OFF"
set "DIST=%SUSHI_ENABLE_DISTRIBUTED%"

REM AddressSanitizer toggle (default OFF), passed via the SUSHI_ASAN env var by
REM the Python CLI's --addsanitizer flag. ASan uses an isolated build tree
REM (build-asan), forces CUDA OFF (the CUDA UR adapter collides with ASan's
REM reserved VA; run under ONEAPI_DEVICE_SELECTOR=opencl:cpu), stays a Release
REM build (so the auto-TSan block never fires), and injects -fsanitize=address.
if "%SUSHI_ASAN%"=="" set "SUSHI_ASAN=OFF"
set "ASAN_FLAGS="
if "%SUSHI_ASAN%"=="ON" (
    set "BUILD_DIR=build-asan"
    set "CUDA_OPT=OFF"
    set "ASAN_FLAGS=-fsanitize=address -fno-omit-frame-pointer -g -O1"
    echo [INFO] AddressSanitizer build: ON ^(CUDA forced OFF, tree=build-asan^)
) else (
    REM Windows builds have always defaulted to CUDA OFF (no Codeplay nvptx
    REM toolchain on the Windows host); keep that behavior.
    set "BUILD_DIR=build"
    set "CUDA_OPT=OFF"
)

set "NEED_CONFIG=0"
if not exist "%BUILD_DIR%\build.ninja" set "NEED_CONFIG=1"
set "PREV_DIST="
if exist "%BUILD_DIR%\.sushi_dist" set /p PREV_DIST=<"%BUILD_DIR%\.sushi_dist"
if not "%PREV_DIST%"=="%DIST%" set "NEED_CONFIG=1"

if "%NEED_CONFIG%"=="1" (
    echo [INFO] Configuring CMake... ^(distributed=%DIST%, asan=%SUSHI_ASAN%^)
    if exist "%BUILD_DIR%" rmdir /s /q "%BUILD_DIR%"

    cmake -B "%BUILD_DIR%" -G Ninja ^
        -DCMAKE_BUILD_TYPE=Release ^
        -DCMAKE_TOOLCHAIN_FILE="%CMAKE_TOOLCHAIN_FILE%" ^
        -DVCPKG_ROOT="%VCPKG_ROOT%" ^
        -DVCPKG_TARGET_TRIPLET=%CFG_TRIPLET% ^
        -DCMAKE_MAKE_PROGRAM="%CFG_NINJA%" ^
        -DCMAKE_CXX_COMPILER="%CFG_ICX%" ^
        -DCMAKE_C_COMPILER="%CFG_ICX%" ^
        -DCMAKE_CXX_FLAGS="-fsycl %ASAN_FLAGS%" ^
        -DCMAKE_C_FLAGS="%ASAN_FLAGS%" ^
        -DPKG_CONFIG_EXECUTABLE="%CFG_PKGCONF%" ^
        -DCMAKE_PREFIX_PATH="%CFG_VCPKG%\installed\%CFG_TRIPLET%" ^
        -DSUSHI_ENABLE_CUDA=%CUDA_OPT% ^
        -DSUSHI_ENABLE_DISTRIBUTED=%DIST% ^
        -DSYCL_LIB_SUFFIX=9
    if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"
    echo %DIST%>"%BUILD_DIR%\.sushi_dist"
)

echo [INFO] Building...
cmake --build "%BUILD_DIR%" --config Release

if %ERRORLEVEL% EQU 0 (
    echo [SUCCESS] Build completed successfully!
) else (
    echo [ERROR] Build failed.
)

popd
pause
