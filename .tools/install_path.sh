#!/bin/bash

# Get the absolute path of the current directory (.tools)
TOOL_PATH="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"

echo "[INFO] Adding $TOOL_PATH to ~/.bashrc..."

# Check if it's already in .bashrc
if grep -q "$TOOL_PATH" ~/.bashrc; then
    echo "[INFO] Path already exists in ~/.bashrc"
else
    echo "" >> ~/.bashrc
    echo "# SushiRuntime Manager Path" >> ~/.bashrc
    echo "export PATH=\"\$PATH:$TOOL_PATH\"" >> ~/.bashrc
    echo "[SUCCESS] Path added to ~/.bashrc."
fi

echo ""
echo "[IMPORTANT] Please run 'source ~/.bashrc' or restart your terminal for changes to take effect."
