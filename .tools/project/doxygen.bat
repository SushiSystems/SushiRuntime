@echo off
setlocal

pushd %~dp0

where doxygen >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Doxygen not found in PATH!
    popd
    pause
    exit /b 1
)

cd ..\..

if not exist "docs" mkdir docs

echo [INFO] Generating documentation...
doxygen Doxyfile

if %ERRORLEVEL% EQU 0 (
    echo [SUCCESS] Documentation generated in docs/ directory.
) else (
    echo [ERROR] Doxygen failed.
)

popd
pause
