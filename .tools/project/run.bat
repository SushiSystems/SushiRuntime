@echo off
setlocal enabledelayedexpansion

:: Switch to script directory
pushd %~dp0

:: 1. Load configuration from config.ini
if not exist "..\config.ini" (
    echo [ERROR] config.ini not found in .tools/ folder!
    echo [HINT] Please copy config.ini.template to config.ini in .tools/ and edit it.
    goto end
)

for /f "usebackq tokens=1,2 delims==" %%A in ("..\config.ini") do (
    if "%%A"=="ONEAPI_ROOT" set "ONEAPI_ROOT=%%B"
    if "%%A"=="VCPKG_ROOT" set "VCPKG_ROOT=%%B"
    if "%%A"=="TARGET_BIN" set "DEFAULT_BIN=%%B"
    if "%%A"=="VCPKG_TRIPLET" set "VCPKG_TRIPLET=%%B"
)

:: Set default triplet if not found
if not defined VCPKG_TRIPLET set "VCPKG_TRIPLET=x64-windows"

:: 2. Setup Intel oneAPI environment if needed
if exist "%ONEAPI_ROOT%\setvars.bat" (
    call "%ONEAPI_ROOT%\setvars.bat" intel64 >nul 2>&1
)

:: Add build artifact paths and vcpkg binaries to PATH
set "PATH=%~dp0..\..\build\src;%VCPKG_ROOT%\installed\%VCPKG_TRIPLET%\bin;%PATH%"

set "BUILD_ROOT=..\..\build"
set "SELECTED_EXE="

:: 3. Process Arguments
if "%~1"=="--help" (
    echo.
    echo SushiRuntime Runner Script
    echo Usage: run.bat [option^|executable_name]
    echo.
    echo Options:
    echo   --help, -h    Show this help message
    echo   --sort        Interactive menu to select an executable from build directory
    echo.
    echo Examples:
    echo   run.bat                  Run the default binary defined in config.ini
    echo   run.bat --sort           List and select available executables
    echo   run.bat my_app           Search and run a binary named 'my_app' or 'my_app.exe'
    echo.
    goto end
)
if "%~1"=="-h" (
    goto :help_block
)
goto :skip_help
:help_block
echo SushiRuntime Runner Script
echo Usage: run.bat [option^|executable_name]
goto end
:skip_help

if "%~1"=="--sort" (
    echo.
    echo ============================================================
    echo           AVAILABLE EXECUTABLES IN BUILD DIRECTORY
    echo ============================================================
    
    set "idx=0"
    for /r "%BUILD_ROOT%" %%F in (*.exe) do (
        set "fpath=%%F"
        :: Filter out internal CMake, vcpkg, and build infrastructure files
        echo !fpath! | findstr /i "CMakeFiles vcpkg_installed vcpkg-test-install" >nul
        if errorlevel 1 (
            set /a idx+=1
            set "exe_!idx!=%%F"
            set "exe_name_!idx!=%%~nxF"
            echo [!idx!] %%~nxF
        )
    )
    
    if !idx! equ 0 (
        echo [ERROR] No executables found. Please build the project first.
        goto end
    )
    
    echo ============================================================
    set /p "selection=Please select a number [1-!idx!]: "
    
    :: Use a for loop to safely dereference the index-based variable
    for %%s in (!selection!) do (
        if defined exe_%%s (
            set "SELECTED_EXE=!exe_%%s!"
        ) else (
            echo [ERROR] Invalid selection index: %selection%
            goto end
        )
    )
) else if not "%~1"=="" (
    :: User provided a specific binary name (potentially with .cpp or without .exe)
    set "QUERY=%~1"
    :: Strip .cpp if provided
    set "QUERY=!QUERY:.cpp=!"
    
    :: Search for it
    for /r "%BUILD_ROOT%" %%F in (*.exe) do (
        set "fname=%%~nxF"
        set "fpath=%%F"
        
        :: Check for exact matches
        if /i "!fname!"=="!QUERY!" set "SELECTED_EXE=%%F"
        if /i "!fname!"=="!QUERY!.exe" set "SELECTED_EXE=%%F"
        
        :: If not found yet, check if the query is a substring (fallback)
        if not defined SELECTED_EXE (
            echo !fname! | findstr /i "!QUERY!" >nul
            if not errorlevel 1 (
                echo !fpath! | findstr /i "CMakeFiles" >nul
                if errorlevel 1 set "SELECTED_EXE=%%F"
            )
        )
    )
    
    if not defined SELECTED_EXE (
        echo [ERROR] Executable matching '%~1' could not be found.
        goto end
    )
) else (
    :: No arguments, use default from config
    if not defined DEFAULT_BIN (
        echo [ERROR] No TARGET_BIN defined in config.ini and no argument provided.
        goto end
    )
    
    :: Look for the default binary
    for /r "%BUILD_ROOT%" %%F in (*.exe) do (
        set "fname=%%~nxF"
        if /i "!fname!"=="%DEFAULT_BIN%" set "SELECTED_EXE=%%F"
        if /i "!fname!"=="%DEFAULT_BIN%.exe" set "SELECTED_EXE=%%F"
    )
)

:: 4. Final Execution
if defined SELECTED_EXE (
    for %%I in ("!SELECTED_EXE!") do echo [INFO] Executing: %%~nxI
    echo [INFO] Path: !SELECTED_EXE!
    echo.
    "!SELECTED_EXE!"
) else (
    echo [ERROR] Binary not found. Run scripts/windows/build.bat first.
)

:end
popd
:: Only pause if run without arguments (likely double-clicked)
if "%~1"=="" pause
