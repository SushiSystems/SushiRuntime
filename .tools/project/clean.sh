#!/bin/bash
# SushiRuntime Build Cleaning Script
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
BUILD_DIR="$DIR/../../build"

echo "[INFO] Removing build directory..."

if [ -d "$BUILD_DIR" ]; then
    rm -rf "$BUILD_DIR"
    echo "[SUCCESS] Build directory removed."
else
    echo "[INFO] No build directory found."
fi
