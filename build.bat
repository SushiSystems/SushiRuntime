@echo off
setlocal

:: 1. OneAPI Ortamini Yukle
call "C:\Program Files (x86)\Intel\oneAPI\setvars.bat"

:: 2. Ninja'yi PATH'e Ekle
:: Oncelikle vcpkg iindeki ninja'yi deneyelim
set "NINJA_PATH=C:\vcpkg\downloads\tools\ninja-1.13.2-windows"
if not exist "%NINJA_PATH%\ninja.exe" (
    :: Alternatif: VS iindeki ninja
    set "NINJA_PATH=C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja"
)
set "PATH=%NINJA_PATH%;%PATH%"

:: 3. Proje DLL'lerini PATH'e Ekle
set "PATH=%~dp0build\src;C:\vcpkg\installed\x64-windows\bin;%PATH%"

:: 4. Build Klasorunu Hazirla ve Derle
if not exist build (
    echo [INFO] Configuring CMake...
    cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
)

echo [INFO] Building...
cmake --build build

if %ERRORLEVEL% EQU 0 (
    echo [SUCCESS] Build completed!
) else (
    echo [ERROR] Build failed.
)
pause
