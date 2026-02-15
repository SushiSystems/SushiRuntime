#!/bin/bash

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
cd "$SCRIPT_DIR"

if [ -f "config.cfg" ]; then
    while IFS='=' read -r key value; do
        [[ $key =~ ^#.* ]] || [[ -z $key ]] && continue
        export "$key=$value"
    done < config.cfg
fi

if [ -f "$ONEAPI_ROOT/setvars.sh" ]; then
    source "$ONEAPI_ROOT/setvars.sh" --force > /dev/null 2>&1
fi

export LD_LIBRARY_PATH="$SCRIPT_DIR/../../build/src:$LD_LIBRARY_PATH"

if [ -f "../../build/examples/$TARGET_BIN" ]; then
    echo "[INFO] Running $TARGET_BIN..."
    "../../build/examples/$TARGET_BIN"
else
    echo "[ERROR] Binary $TARGET_BIN not found. Run scripts/linux/build.sh first."
fi
