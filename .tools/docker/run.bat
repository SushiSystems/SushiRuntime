@echo off
echo [INFO] Starting SushiRuntime developer container with volume...
docker run -it ^
    -v "%CD%:/workspace/sushiruntime" ^
    sushiruntime /bin/bash
