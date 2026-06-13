#!/bin/bash
# SushiRuntime Build Script
set -e

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
CONFIG_FILE="$DIR/../config.cfg"

if [ ! -f "$CONFIG_FILE" ]; then
    echo "[ERROR] config.cfg not found in .tools/ folder!"
    echo "[HINT] Please copy config.cfg.template to config.cfg in .tools/ folder and edit it."
    exit 1
fi

# Parse config.cfg
CFG_CC="icx"
CFG_CXX="icpx"
CFG_GEN="Ninja"
CFG_USE_VCPKG="false"

while IFS='=' read -r key value || [ -n "$key" ]; do
    [[ "$key" =~ ^#.*$ ]] && continue
    [[ -z "$key" ]] && continue
    
    key=$(echo "$key" | tr -d '\r' | xargs)
    value=$(echo "$value" | tr -d '\r' | xargs)
    
    case "$key" in
        "CC") CFG_CC="$value" ;;
        "CXX") CFG_CXX="$value" ;;
        "ONEAPI_ROOT") CFG_ONEAPI="$value" ;;
        "VCPKG_ROOT") CFG_VCPKG="$value" ;;
        "CMAKE_GENERATOR") CFG_GEN="$value" ;;
        "USE_VCPKG") CFG_USE_VCPKG="$value" ;;
    esac
done < "$CONFIG_FILE"

# Source oneAPI environment
if [ -d "$CFG_ONEAPI" ] && [ -f "$CFG_ONEAPI/setvars.sh" ]; then
    echo "[INFO] Loading oneAPI vars..."
    source "$CFG_ONEAPI/setvars.sh" --force > /dev/null 2>&1 || true
fi

cd "$DIR/../.."

# Distributed layer toggle (default OFF). Passed in via env by the Python CLI's
# `--distributed` flag. It changes Graph::Node's layout (PUBLIC compile def), so
# flipping it requires a fresh configure — we track the last value in a stamp.
DIST="${SUSHI_ENABLE_DISTRIBUTED:-OFF}"
STAMP="build/.sushi_dist"

NEED_CONFIG=false
[ ! -d "build" ] && NEED_CONFIG=true
[ "$CFG_GEN" == "Ninja" ] && [ ! -f "build/build.ninja" ] && NEED_CONFIG=true
[ "$CFG_GEN" != "Ninja" ] && [ ! -f "build/Makefile" ] && NEED_CONFIG=true
if [ -f "$STAMP" ]; then
    [ "$(cat "$STAMP" 2>/dev/null)" != "$DIST" ] && NEED_CONFIG=true
elif [ "$DIST" != "OFF" ]; then
    NEED_CONFIG=true
fi

# Configure if needed
if [ "$NEED_CONFIG" = true ]; then
    echo "[INFO] Configuring CMake... (distributed=$DIST)"

    # If build exists but is invalid, recreate it
    if [ -d "build" ]; then
        rm -rf build
    fi

    # In Docker (intel/llvm), override icpx/icx with clang++/clang if not found
    if ! command -v "$CFG_CXX" &>/dev/null && command -v clang++ &>/dev/null; then
        CFG_CXX="clang++"
        CFG_CC="clang"
        echo "[INFO] icpx not found, using intel/llvm clang++ instead."
    fi

    CMAKE_ARGS=("-B" "build" "-G" "$CFG_GEN" "-DCMAKE_BUILD_TYPE=Release")
    CMAKE_ARGS+=("-DCMAKE_C_COMPILER=$CFG_CC")
    CMAKE_ARGS+=("-DCMAKE_CXX_COMPILER=$CFG_CXX")
    CMAKE_ARGS+=("-DSUSHI_ENABLE_CUDA=ON")
    CMAKE_ARGS+=("-DSUSHI_ENABLE_DISTRIBUTED=$DIST")

    if [ "$CFG_USE_VCPKG" == "true" ]; then
        CMAKE_ARGS+=("-DCMAKE_TOOLCHAIN_FILE=$CFG_VCPKG/scripts/buildsystems/vcpkg.cmake")
        CMAKE_ARGS+=("-DVCPKG_ROOT=$CFG_VCPKG")
    fi

    cmake "${CMAKE_ARGS[@]}"
    mkdir -p build
    echo "$DIST" > "$STAMP"
fi

echo "[INFO] Building..."
cmake --build build --config Release

if [ $? -eq 0 ]; then
    echo "[SUCCESS] Build completed successfully!"
else
    echo "[ERROR] Build failed."
    exit 1
fi
