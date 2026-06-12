#!/bin/bash

# Get the absolute path of the current directory (.tools)
TOOL_PATH="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"

echo -e "\033[34m[INFO] Removing SushiRuntime Manager Path: $TOOL_PATH\033[0m"

# Pick the shell rc file
if [ -n "$ZSH_VERSION" ] || [ "$(basename "$SHELL")" = "zsh" ]; then
    RC_FILE="$HOME/.zshrc"
else
    RC_FILE="$HOME/.bashrc"
fi

# 1. Remove the persistent PATH entry from the shell rc file
if [ -f "$RC_FILE" ] && grep -q "# SushiRuntime Manager Path" "$RC_FILE"; then
    # Delete the comment line and the export line that follows it
    sed -i.bak '/# SushiRuntime Manager Path/{N;d;}' "$RC_FILE"
    rm -f "$RC_FILE.bak"
    echo -e "\033[32m[SUCCESS] Path removed from $RC_FILE permanently.\033[0m"
    echo -e "\033[33m[IMPORTANT] Run 'source $RC_FILE' or restart your terminal.\033[0m"
else
    echo -e "\033[36m[INFO] Path not found in $RC_FILE.\033[0m"
fi

# 2. Remove the generated 'sr' command
if [ -e "$TOOL_PATH/sr" ]; then
    rm -f "$TOOL_PATH/sr"
    echo -e "\033[32m[SUCCESS] Removed 'sr' command.\033[0m"
fi

# 3. Remove TOOL_PATH from the current session path
NEW_PATH=""
IFS=':' read -ra PARTS <<< "$PATH"
for p in "${PARTS[@]}"; do
    if [ -n "$p" ] && [ "$p" != "$TOOL_PATH" ]; then
        NEW_PATH="${NEW_PATH:+$NEW_PATH:}$p"
    fi
done
export PATH="$NEW_PATH"
echo -e "\033[32m[SUCCESS] Path removed from the current session.\033[0m"
