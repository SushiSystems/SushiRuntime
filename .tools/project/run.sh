#!/bin/bash
# SushiRuntime Runner Script
set -e

# CUDA_LAUNCH_BLOCKING=1 forces every GPU kernel to run synchronously. That quietly
# breaks our async/overlap tests: submit() looks like it blocks until the kernel is
# done, so no concurrency is ever observed. A container we ran in had it set globally
# and burned us for a while. Clear it here so a stray value can never poison a run.
unset CUDA_LAUNCH_BLOCKING

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
CONFIG_FILE="$DIR/../config.cfg"

if [ ! -f "$CONFIG_FILE" ]; then
    echo "[ERROR] config.cfg not found in .tools/ folder!"
    echo "[HINT] Please copy config.cfg.template to config.cfg in .tools/ and edit it."
    exit 1
fi

# 1. Load configuration
while IFS='=' read -r key value || [ -n "$key" ]; do
    [[ "$key" =~ ^#.*$ ]] && continue
    [[ -z "$key" ]] && continue
    key=$(echo "$key" | tr -d '\r' | xargs)
    value=$(echo "$value" | tr -d '\r' | xargs)
    
    case "$key" in
        "ONEAPI_ROOT") ONEAPI_ROOT="$value" ;;
        "TARGET_BIN") DEFAULT_BIN="$value" ;;
    esac
done < "$CONFIG_FILE"

# 2. Setup Intel oneAPI environment if needed
if [ -d "$ONEAPI_ROOT" ] && [ -f "$ONEAPI_ROOT/setvars.sh" ]; then
    source "$ONEAPI_ROOT/setvars.sh" --force > /dev/null 2>&1 || true
fi

# AddressSanitizer run mode (SUSHI_ASAN=ON, set by the CLI's --addsanitizer).
# The ASan binaries live in build-asan/ and were built with CUDA OFF. The SYCL
# runtime would still try to load the installed CUDA UR adapter, whose
# urVirtualMemReserve collides with ASan's reserved VA — so we pin the backend to
# the OpenCL CPU device, which loads only the OpenCL adapter (CUDA never inits).
ASAN="${SUSHI_ASAN:-OFF}"
if [ "$ASAN" == "ON" ]; then
    BUILD_NAME="build-asan"
    export ONEAPI_DEVICE_SELECTOR="${ONEAPI_DEVICE_SELECTOR:-opencl:cpu}"
    # Leak detection off by default (SYCL/UR hold pools to process exit, which
    # ASan would report as leaks); the use-after-free/overflow checks stay on.
    export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0:abort_on_error=1}"
    echo "[INFO] AddressSanitizer run: tree=build-asan, ONEAPI_DEVICE_SELECTOR=$ONEAPI_DEVICE_SELECTOR"
else
    BUILD_NAME="build"
fi

# Add build artifact paths to PATH
export PATH="$DIR/../../$BUILD_NAME/src:$PATH"
BUILD_ROOT="$DIR/../../$BUILD_NAME"
SELECTED_EXE=""

# 3. Process Arguments
if [[ "$1" == "--help" || "$1" == "-h" ]]; then
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

if [[ "$1" == "--sort" ]]; then
    echo ""
    echo "============================================================"
    echo "          AVAILABLE EXECUTABLES IN BUILD DIRECTORY"
    echo "============================================================"
    
    # Find executables, excluding common build infrastructure folders
    # We use -executable for files with execute bits, and -type f for regular files
    if ! command -v find &> /dev/null; then
        echo "[ERROR] 'find' command not found."
        exit 1
    fi

    exes=()
    while IFS= read -r line; do
        exes+=("$line")
    done < <(find "$BUILD_ROOT" -maxdepth 4 -type f -executable ! -path "*/CMakeFiles/*" ! -name ".*" ! -path "*/vcpkg_installed/*" ! -name "*.cmake" ! -name "*.ninja" ! -name "*.log" ! -name "*.txt" ! -name "*.json" ! -name "*.so*" ! -name "*.a" 2>/dev/null)
    
    if [ ${#exes[@]} -eq 0 ]; then
        echo "[ERROR] No executables found. Please build the project first."
        exit 1
    fi
    
    for i in "${!exes[@]}"; do
        echo "[$((i+1))] $(basename "${exes[$i]}")"
    done
    
    echo "============================================================"
    read -p "Please select a number [1-${#exes[@]}]: " selection
    
    if [[ "$selection" =~ ^[0-9]+$ ]] && [ "$selection" -ge 1 ] && [ "$selection" -le "${#exes[@]}" ]; then
        SELECTED_EXE="${exes[$((selection-1))]}"
    else
        echo "[ERROR] Invalid selection index: $selection"
        exit 1
    fi
elif [ -n "$1" ]; then
    # User provided a specific binary name
    QUERY="$1"
    # Search for exact match
    SELECTED_EXE=$(find "$BUILD_ROOT" -maxdepth 4 -type f -executable ! -path "*/CMakeFiles/*" ! -name ".*" ! -name "*.cmake" ! -name "*.ninja" ! -name "*.log" ! -name "*.txt" ! -name "*.json" ! -name "*.so*" ! -name "*.a" -name "$QUERY" | head -n 1)

    # Fallback to substring search if not found
    if [ -z "$SELECTED_EXE" ]; then
        SELECTED_EXE=$(find "$BUILD_ROOT" -maxdepth 4 -type f -executable ! -path "*/CMakeFiles/*" ! -name ".*" ! -name "*.cmake" ! -name "*.ninja" ! -name "*.log" ! -name "*.txt" ! -name "*.json" ! -name "*.so*" ! -name "*.a" -name "*$QUERY*" | head -n 1)
    fi
    
    if [ -z "$SELECTED_EXE" ]; then
        echo "[ERROR] Executable matching '$1' could not be found."
        exit 1
    fi
else
    # No arguments, use default from config
    if [ -z "$DEFAULT_BIN" ]; then
        echo "[ERROR] No TARGET_BIN defined in config.cfg and no argument provided."
        exit 1
    fi
    
    SELECTED_EXE=$(find "$BUILD_ROOT" -maxdepth 4 -type f -executable ! -path "*/CMakeFiles/*" ! -name ".*" ! -name "*.cmake" ! -name "*.ninja" ! -name "*.log" ! -name "*.txt" ! -name "*.json" ! -name "*.so*" ! -name "*.a" -name "$DEFAULT_BIN" | head -n 1)
fi

# 4. Final Execution
# Forward any args after the binary name straight to the executable, so e.g.
#   run.sh sr_benchmark_tests --gtest_filter='AllocatorBench.*'
# runs just the allocator table without the heavy stress tests.
if [ -n "$SELECTED_EXE" ]; then
    echo "[INFO] Executing: $(basename "$SELECTED_EXE")"
    echo "[INFO] Path: $SELECTED_EXE"
    echo ""
    "$SELECTED_EXE" "${@:2}"
else
    echo "[ERROR] Binary not found. Run .tools/project/build.sh first."
    exit 1
fi
