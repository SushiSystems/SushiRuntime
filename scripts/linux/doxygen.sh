#!/bin/bash

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
cd "$SCRIPT_DIR"

if ! command -v doxygen &> /dev/null; then
    echo "[ERROR] Doxygen not found in PATH! Install it using: sudo apt install doxygen"
    exit 1
fi

cd ../..

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
