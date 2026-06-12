#!/bin/bash

# Get the absolute path of the current directory (.tools)
TOOL_PATH="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"

# Parse args
PERMANENT=false
for arg in "$@"; do
    if [ "$arg" = "--permanent" ]; then
        PERMANENT=true
    fi
done

echo -e "\033[34m--- SushiRuntime Linux Installer ---\033[0m"

# Make the wrapper executable
chmod +x "$TOOL_PATH/sr.sh" 2>/dev/null

# Create an 'sr' command alongside the wrapper so it works like on Windows
if [ ! -e "$TOOL_PATH/sr" ]; then
    ln -s "$TOOL_PATH/sr.sh" "$TOOL_PATH/sr" 2>/dev/null \
        || cp "$TOOL_PATH/sr.sh" "$TOOL_PATH/sr"
fi
chmod +x "$TOOL_PATH/sr" 2>/dev/null

# Pick the shell rc file
if [ -n "$ZSH_VERSION" ] || [ "$(basename "$SHELL")" = "zsh" ]; then
    RC_FILE="$HOME/.zshrc"
else
    RC_FILE="$HOME/.bashrc"
fi

if [ "$PERMANENT" = true ]; then
    echo -e "\033[33m[INFO] Performing permanent installation...\033[0m"

    # 1. Update PATH persistently via the shell rc file
    if grep -q "# SushiRuntime Manager Path" "$RC_FILE" 2>/dev/null; then
        echo -e "\033[36m[INFO] Path already exists in $RC_FILE.\033[0m"
    else
        {
            echo ""
            echo "# SushiRuntime Manager Path"
            echo "export PATH=\"\$PATH:$TOOL_PATH\""
        } >> "$RC_FILE"
        echo -e "\033[32m[SUCCESS] Path added to $RC_FILE permanently.\033[0m"
        echo -e "\033[33m[IMPORTANT] Run 'source $RC_FILE' or restart your terminal.\033[0m"
    fi
else
    echo -e "\033[90m[INFO] Temporary mode. To install permanently, run: ./install.sh --permanent\033[0m"
fi

# 2. Always update CURRENT SESSION path immediately
case ":$PATH:" in
    *":$TOOL_PATH:"*)
        ;;
    *)
        export PATH="$PATH:$TOOL_PATH"
        echo -e "\033[32m[SUCCESS] Path updated for the current session.\033[0m"
        ;;
esac

echo -e "\033[37m\nTry running: sr --help\033[0m"
