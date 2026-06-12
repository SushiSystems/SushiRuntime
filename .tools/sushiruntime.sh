#!/bin/bash
# SushiRuntime wrapper script
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
python3 "$DIR/sushiruntime.py" "$@"
