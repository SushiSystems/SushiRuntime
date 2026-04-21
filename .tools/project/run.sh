#!/bin/bash

# Switch to script directory
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
cd "$SCRIPT_DIR"

# 1. Load configuration
if [ ! -f "../config.cfg" ]; then
    echo "[ERROR] config.cfg not found in .tools/ folder!"
    echo "[HINT] Please copy config.cfg.template to config.cfg in .tools/ and edit it."
    exit 1
fi

while IFS='=' read -r key value; do
    # Skip comments, empty lines, and INI-style headers
    [[ $key =~ ^#.* ]] || [[ $key =~ ^\[.* ]] || [[ -z $key ]] && continue
    # Strip carriage return and whitespace
    key=$(echo "$key" | tr -d '\r' | xargs)
    value=$(echo "$value" | tr -d '\r' | xargs)
    export "$key=$value"
done < ../config.cfg

# 2. Environment Setup
if [ -f "$ONEAPI_ROOT/setvars.sh" ]; then
    source "$ONEAPI_ROOT/setvars.sh" --force > /dev/null 2>&1
fi
export LD_LIBRARY_PATH="$SCRIPT_DIR/../../build/src:$SCRIPT_DIR/../../build/examples:$LD_LIBRARY_PATH"

BUILD_ROOT="../../build"
SELECTED_EXE=""

# 3. Argument Handling
if [[ "$1" == "--help" ]] || [[ "$1" == "-h" ]]; then
    echo ""
    echo "SushiRuntime Runner Script"
    echo "Usage: ./run.sh [option|executable_name]"
    echo ""
    echo "Options:"
    echo "  --help, -h    Show this help message"
    echo "  --sort        Interactive menu to select an executable from build directory"
    echo ""
    echo "Examples:"
    echo "  ./run.sh                  Run the default binary defined in config.cfg"
    echo "  ./run.sh --sort           List and select available executables"
    echo "  ./run.sh my_app           Search and run a binary named 'my_app'"
    echo ""
    exit 0
fi

if [ "$1" == "--sort" ]; then
    echo ""
    echo "============================================================"
    echo "          AVAILABLE EXECUTABLES IN BUILD DIRECTORY          "
    echo "============================================================"
    
    # Find executables excluding CMakeFiles, vcpkg, and libraries (.so, .a)
    mapfile -t BINS < <(find "$BUILD_ROOT" -maxdepth 4 -type f -executable ! -path "*/CMakeFiles/*" ! -path "*/vcpkg_installed/*" ! -name "*.so" ! -name "*.so.*" ! -name "*.a")
    
    if [ ${#BINS[@]} -eq 0 ]; then
        echo "[ERROR] No executables found. Please build the project first."
        exit 1
    fi
    
    for i in "${!BINS[@]}"; do
        echo "[$((i+1))] $(basename "${BINS[$i]}")"
    done
    
    echo "============================================================"
    read -p "Please select a number [1-${#BINS[@]}]: " choice
    
    if [[ "$choice" =~ ^[0-9]+$ ]] && [ "$choice" -ge 1 ] && [ "$choice" -le "${#BINS[@]}" ]; then
        SELECTED_EXE="${BINS[$((choice-1))]}"
    else
        echo "[ERROR] Invalid selection index: $choice"
        exit 1
    fi
elif [ -n "$1" ]; then
    # User provided a name
    QUERY=$(basename "$1" .cpp)
    # Search for an executable with that name
    SELECTED_EXE=$(find "$BUILD_ROOT" -maxdepth 4 -type f -executable ! -path "*/CMakeFiles/*" -name "$QUERY" -print -quit)
    
    if [ -z "$SELECTED_EXE" ]; then
        # Check if it exists with suffix or as substring
        SELECTED_EXE=$(find "$BUILD_ROOT" -maxdepth 4 -type f -executable ! -path "*/CMakeFiles/*" -name "*$QUERY*" -print -quit)
    fi
    
    if [ -z "$SELECTED_EXE" ]; then
        echo "[ERROR] Executable matching '$1' could not be found."
        exit 1
    fi
else
    # Default from config
    if [ -z "$TARGET_BIN" ]; then
        echo "[ERROR] No TARGET_BIN defined in config.cfg and no argument provided."
        exit 1
    fi
    
    # Search for the default binary
    SELECTED_EXE=$(find "$BUILD_ROOT" -maxdepth 4 -type f -executable ! -path "*/CMakeFiles/*" -name "$TARGET_BIN" -print -quit)
    
    if [ -z "$SELECTED_EXE" ]; then
        # Try without extension if it was provided in config but linux binary has none
        QUERY=$(basename "$TARGET_BIN" .exe)
        SELECTED_EXE=$(find "$BUILD_ROOT" -maxdepth 4 -type f -executable ! -path "*/CMakeFiles/*" -name "$QUERY" -print -quit)
    fi
fi

# 4. Binary Execution
if [ -n "$SELECTED_EXE" ]; then
    echo "[INFO] Executing: $(basename "$SELECTED_EXE")"
    echo "[INFO] Path: $SELECTED_EXE"
    echo ""
    "$SELECTED_EXE"
else
    echo "[ERROR] Binary not found. Run build script first."
    exit 1
fi
