@echo off
echo [INFO] Building SushiRuntime Docker image...
docker build -t sushiruntime .
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Docker build failed.
    pause
    exit /b %ERRORLEVEL%
)
echo [SUCCESS] Docker image built successfully.
pause
