@echo off
echo [INFO] Removing build directory...

if exist "%~dp0..\..\build" (
    rmdir /s /q "%~dp0..\..\build"
    echo [SUCCESS] Build directory removed.
) else (
    echo [INFO] No build directory found.
)

pause
