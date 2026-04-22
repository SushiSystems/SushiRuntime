#!/bin/bash

# Get the absolute path of the current directory (.tools)
TOOL_PATH="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"

echo "[INFO] Removing $TOOL_PATH from ~/.bashrc..."

# Check if it exists in .bashrc
if grep -q "$TOOL_PATH" ~/.bashrc; then
    # Create a temporary file without the lines related to this path
    # This removes the export line and the comment line immediately preceding it if it matches
    sed -i "\|# SushiRuntime Manager Path|d" ~/.bashrc
    sed -i "\|export PATH=\"\$PATH:$TOOL_PATH\"|d" ~/.bashrc
    
    echo "[SUCCESS] Path removed from ~/.bashrc."
else
    echo "[INFO] Path not found in ~/.bashrc"
fi

echo ""
echo "[IMPORTANT] Please run 'source ~/.bashrc' or restart your terminal for changes to take effect."
