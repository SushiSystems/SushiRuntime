#!/bin/bash
# SushiRuntime Docker Build Script
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
cd "$DIR/../.."

echo "[INFO] Building SushiRuntime Docker image..."
docker build -t sushiruntime .

if [ $? -ne 0 ]; then
    echo "[ERROR] Docker build failed."
    exit 1
fi

echo "[SUCCESS] Docker image built successfully."
