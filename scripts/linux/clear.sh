#!/bin/bash

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
cd "$SCRIPT_DIR/../.."

echo "[INFO] Removing build directory..."

if [ -d "build" ]; then
    rm -rf build
    echo "[SUCCESS] Build directory removed."
else
    echo "[INFO] No build directory found."
fi
