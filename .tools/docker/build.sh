#!/bin/bash
echo "[INFO] Building SushiRuntime Docker image..."
docker build -t sushiruntime .
if [ $? -eq 0 ]; then
    echo "[SUCCESS] Docker image built successfully."
else
    echo "[ERROR] Docker build failed."
    exit 1
fi
