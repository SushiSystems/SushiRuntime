#!/bin/bash
# SushiRuntime Doxygen Generation Script
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
cd "$DIR/../.."

if ! command -v doxygen &> /dev/null; then
    echo "[ERROR] Doxygen not found in PATH!"
    exit 1
fi

if [ ! -d "docs" ]; then
    mkdir docs
fi

echo "[INFO] Generating documentation..."
doxygen Doxyfile

if [ $? -eq 0 ]; then
    echo "[SUCCESS] Documentation generated in docs/ directory."
else
    echo "[ERROR] Doxygen failed."
fi
